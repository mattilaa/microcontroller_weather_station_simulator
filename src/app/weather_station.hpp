#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app/units.hpp"
#include "app/weather_model.hpp"
#include "util/random.hpp"
#include "util/ring_buffer.hpp"

namespace app
{

// Cloud layers by type, each at its own level (see weather_model): amounts in
// whole percent of the sky, levels as pressure.
struct cloud_layers
{
    percent cirrus;       // high: cirrus_base..cirrus_top
    percent altostratus;  // middle: altostratus_base..altostratus_top
    percent cumulus;      // from the LCL up to cumulus_top
    percent cumulonimbus; // from the LCL up to the tropopause (200 hPa)
    hectopascal lcl;      // lifting condensation level: the cloud base
    hectopascal cumulus_top;
    hectopascal cirrus_base;
    hectopascal cirrus_top;
    hectopascal altostratus_base;
    hectopascal altostratus_top;
    percent stratus;       // low sheet: from the LCL up to stratus_top
    percent stratocumulus; // low lumpy layer: LCL..stratocumulus_top
    percent nimbostratus;  // rain cloud: LCL..nimbostratus_top
    percent altocumulus;   // patchy middle: altocumulus_base..top
    // Derived from the values above (weather_model formulas), not measured:
    // (default-initialized; derive_cloud_levels() fills them in)
    hectopascal stratus_top{};
    hectopascal stratocumulus_top{};
    hectopascal nimbostratus_top{};
    hectopascal altocumulus_base{};
    hectopascal altocumulus_top{};
};

// Fill in the derived levels of `c` from its LCL, amounts and altostratus
// levels.
void derive_cloud_levels(cloud_layers& c);

// One set of sensor readings: a single sample, a 10-minute record, or the
// mean of the current 10-minute interval.
struct weather_reading
{
    uint32_t day;
    clock_time time;
    centi_celsius temperature;
    centi_percent humidity;
    centi_mps wind_speed; // samples: the moment; records: the mean
    centi_mps wind_gust;  // highest peak (gust) wind seen in the interval
    compass_degrees wind_direction;
    centi_mm rain;       // samples: since the previous update; records: in
                         // the 10-minute interval
    centi_mm rain_today; // since midnight
    percent cloud_cover; // of the sky, all layers
    deci_hectopascal pressure; // surface pressure
    cloud_layers clouds;
};

// "06:10:05 temp 12.34 C | hum 71.20 %RH | wind 3.42 m/s from 225 deg SW |
//  rain 0.40 mm | cloud 63 % | 1013.2 hPa"
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
    // 256 records, 42.7 simulated hours. (ring_buffer needs a power of two.)
    static constexpr size_t CAPACITY = 256;

    // One record, packed into small integers to fit the RAM budget (44
    // bytes instead of a full weather_reading's 124). rain_today and the
    // derived cloud levels aren't kept.
    struct entry
    {
        uint32_t sequence;
        uint16_t day;
        uint16_t minute_of_day;
        int16_t temperature;       // 0.01 degC
        uint16_t humidity;         // 0.01 %RH
        uint16_t wind_speed;       // 0.01 m/s, mean
        uint16_t wind_gust;        // 0.01 m/s, highest in the interval
        uint16_t wind_direction;   // degrees
        uint16_t rain;             // 0.01 mm in the interval
        uint16_t lcl;              // hPa
        uint16_t cumulus_top;      // hPa
        uint16_t cirrus_base;      // hPa
        uint16_t cirrus_top;       // hPa
        uint16_t altostratus_base; // hPa
        uint16_t altostratus_top;  // hPa
        uint8_t cloud_cover;       // %
        uint8_t cirrus;            // %
        uint8_t altostratus;       // %
        uint8_t cumulus;           // %
        uint8_t cumulonimbus;      // %
        uint8_t stratus;           // %
        uint8_t stratocumulus;     // %
        uint8_t nimbostratus;      // %
        uint8_t altocumulus;       // %
        uint16_t pressure;         // 0.1 hPa

        weather_reading reading() const;
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
// tipping-bucket rain gauge, sky imager (cloud cover and cloud types) and
// ceilometer (cloud base and tops) measuring the simulated weather.
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

    // Switch the weather scenario (and take a fresh sample).
    void set_scenario(const weather_scenario& scenario);

    const weather_scenario& scenario() const
    {
        return _model.scenario();
    }

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
    void reset_interval();
    weather_reading mean_of_interval() const; // without derived levels

    weather_model _model;
    sensor_model _thermometer{5, 1, -4000, 6000, 0x7e57'0001u}; // +/-0.05 C
    sensor_model _hygrometer{30, 10, 0, 10000, 0x7e57'0002u};   // +/-0.3 %
    sensor_model _anemometer{5, 10, 0, 6000, 0x7e57'0003u};     // 0.1 m/s
    sensor_model _wind_vane{2, 1, -720, 1080, 0x7e57'0004u};    // 1 deg
    sensor_model _sky_imager{2, 1, 0, 100, 0x7e57'0005u};       // 1 %
    sensor_model _cloud_sensor{1, 1, 0, 100, 0x7e57'0006u};     // layers, 1 %
    sensor_model _ceilometer{2, 1, 150, 1100, 0x7e57'0007u};    // levels, hPa
    sensor_model _barometer{1, 1, 8700, 10850, 0x7e57'0008u};   // 0.1 hPa

    uint32_t _speed = DEFAULT_SPEED;
    uint64_t _last_update_ms = 0;
    uint64_t _unsimulated_ms = 0; // simulated ms not yet simulated (<1 s)

    // The 10-minute interval in progress: time-weighted sums.
    int64_t _sum_temperature = 0;
    int64_t _sum_humidity = 0;
    int64_t _sum_wind_speed = 0;
    int32_t _interval_gust = 0; // highest peak wind this interval, 0.01 m/s
    int64_t _sum_cloud_cover = 0;
    int64_t _sum_pressure = 0;
    int64_t _sum_cirrus = 0;
    int64_t _sum_altostratus = 0;
    int64_t _sum_cumulus = 0;
    int64_t _sum_cumulonimbus = 0;
    int64_t _sum_lcl = 0;
    int64_t _sum_cumulus_top = 0;
    int64_t _sum_cirrus_base = 0;
    int64_t _sum_cirrus_top = 0;
    int64_t _sum_altostratus_base = 0;
    int64_t _sum_altostratus_top = 0;
    int64_t _sum_stratus = 0;
    int64_t _sum_stratocumulus = 0;
    int64_t _sum_nimbostratus = 0;
    int64_t _sum_altocumulus = 0;
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
