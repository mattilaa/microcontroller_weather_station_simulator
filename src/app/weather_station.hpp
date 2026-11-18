#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app/units.hpp"
#include "app/weather_model.hpp"
#include "util/random.hpp"
#include "util/ring_buffer.hpp"

namespace app
{

// One set of sensor readings: a single sample, a 10-minute record, or the
// mean of the current 10-minute interval.
struct weather_reading
{
    uint32_t day;
    clock_time time;
    centi_celsius temperature;
    centi_percent humidity;
    centi_mps wind_speed;
    compass_degrees wind_direction;
    centi_mm rain;       // samples: since the previous update; records: in
                         // the 10-minute interval
    centi_mm rain_today; // since midnight
    percent cloud_cover; // of the sky
};

// "06:10:05 temp 12.34 C | hum 71.20 %RH | wind 3.42 m/s from 225 deg SW |
//  rain 0.40 mm | cloud 63 %"
util::text_stream& operator<<(util::text_stream& out, const weather_reading& r);

// Measurement characteristics of a physical sensor: it sees the true value
// plus its own noise, quantized to its resolution and limited to its range.
class sensor_model
{
public:
    constexpr sensor_model(
        int32_t noise_sigma,
        int32_t resolution,
        int32_t min,
        int32_t max,
        uint32_t seed)
        : _noise_sigma(noise_sigma), _resolution(resolution), _min(min),
          _max(max), _rng(seed)
    {
    }

    int32_t measure(int32_t true_value);

private:
    int32_t _noise_sigma;
    int32_t _resolution;
    int32_t _min;
    int32_t _max;
    util::xorshift32 _rng;
};

// Log of 10-minute weather records, each with a sequence number so clients
// can fetch new records incrementally. Kept apart from weather_station so it
// can be a zero-initialized global in .bss (it is large).
class weather_log
{
public:
    // 256 records of 40 bytes: 10 KiB of the 32 KiB RAM budget, 42.7
    // simulated hours. (ring_buffer needs a power of two.)
    static constexpr size_t CAPACITY = 256;

    struct entry
    {
        uint32_t sequence;
        weather_reading reading;
    };

    void add(const weather_reading& reading);

    size_t size() const
    {
        return _entries.size();
    }

    // Entry i, from the oldest (0) to the newest (size() - 1).
    const entry& operator[](size_t i) const
    {
        return _entries[i];
    }

    // Index of the first entry with a sequence number >= `sequence` (the
    // oldest entry if `sequence` has already been dropped; size() if none).
    size_t find(uint32_t sequence) const;

    uint32_t next_sequence() const
    {
        return _next_sequence;
    }

private:
    util::ring_buffer<entry, CAPACITY> _entries;
    uint32_t _next_sequence = 0;
};

// The weather station: a thermometer, hygrometer, anemometer, wind vane,
// tipping-bucket rain gauge and cloud cover sensor (sky imager / ceilometer)
// measuring the simulated weather.
//
// Like a real automatic weather station it samples its sensors often (once
// per update, i.e. every real second, and at least once per simulated
// minute) and logs one record per 10 simulated minutes: the time-weighted
// mean of the samples (wind direction averaged on the circle) and the rain
// that fell in the interval. A record's time is the end of its interval.
//
// Simulated time runs `speed` times faster than real time: 1 is real time,
// 600 makes every real second ten simulated minutes, 72000 twenty hours.
// At 72000 one update simulates 1200 minutes (1200 model steps and samples,
// 120 records), which takes the firmware a few milliseconds.
class weather_station
{
public:
    static constexpr uint32_t RECORD_SECONDS = 600; // 10-minute records
    static constexpr uint32_t MAX_SAMPLE_SECONDS = 60;
    static constexpr int32_t RAIN_GAUGE_TIP = 20; // 0.2 mm per bucket tip
    static constexpr uint32_t DEFAULT_SPEED = 1;  // real time
    static constexpr uint32_t MAX_SPEED = 72'000; // 20 hours per second

    explicit constexpr weather_station(weather_log& log) : _log(log) {}

    // Simulated seconds per real second, 1..MAX_SPEED (clamped).
    void set_speed(uint32_t speed);

    uint32_t speed() const
    {
        return _speed;
    }

    // Advance the simulation by the real time since the last update (times
    // the speed), sampling the sensors and logging finished records.
    void update(uint64_t now_ms);

    // Jump the simulated clock to a time of day and take a fresh sample.
    // The 10-minute interval in progress is discarded.
    void set_time_of_day(uint32_t minute);

    // Fast-forward: simulate `minutes` instantly (sampling every simulated
    // minute and logging the records), independent of the speed.
    void fast_forward(uint32_t minutes);

    bool has_reading() const
    {
        return _has_reading;
    }

    // The latest sample.
    const weather_reading& latest() const
    {
        return _latest;
    }

    // Mean of the samples in the 10-minute interval in progress.
    weather_reading interval_mean() const;

    const weather_log& log() const
    {
        return _log;
    }

private:
    void simulate(uint64_t sim_seconds);
    void sample(uint32_t weight_seconds);
    void close_record();

    weather_model _model;
    sensor_model _thermometer{5, 1, -4000, 6000, 0x7e57'0001u}; // +/-0.05 C
    sensor_model _hygrometer{30, 10, 0, 10000, 0x7e57'0002u};   // +/-0.3 %
    sensor_model _anemometer{5, 10, 0, 6000, 0x7e57'0003u};     // 0.1 m/s
    sensor_model _wind_vane{2, 1, -720, 1080, 0x7e57'0004u};    // 1 deg
    sensor_model _sky_imager{2, 1, 0, 100, 0x7e57'0005u};       // 1 %

    uint32_t _speed = DEFAULT_SPEED;
    uint64_t _last_update_ms = 0;
    uint64_t _unsimulated_ms = 0; // simulated ms not yet simulated (<1 s)

    // The 10-minute interval in progress: time-weighted sums.
    int64_t _sum_temperature = 0;
    int64_t _sum_humidity = 0;
    int64_t _sum_wind_speed = 0;
    int64_t _sum_cloud_cover = 0;
    int64_t _sum_direction = 0; // of the unwrapped (continuous) direction
    uint32_t _sum_weight = 0;   // seconds
    int32_t _interval_rain = 0;
    int32_t _unwrapped_direction = 0;

    int32_t _rain_bucket = 0; // 0.01 mm collected but not yet tipped
    int32_t _update_rain = 0; // 0.01 mm tipped during the current update
    int32_t _rain_today = 0;  // 0.01 mm

    weather_log& _log;
    weather_reading _latest{};
    bool _has_reading = false;
};

} // namespace app
