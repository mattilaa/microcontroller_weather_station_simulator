#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app/units.hpp"
#include "app/weather_model.hpp"
#include "util/moving_average.hpp"
#include "util/random.hpp"
#include "util/ring_buffer.hpp"

namespace app
{

// One set of sensor readings.
struct weather_reading
{
    uint32_t day;
    clock_time time;
    centi_celsius temperature;
    centi_percent humidity;
    centi_mps wind_speed;
    compass_degrees wind_direction;
    centi_mm rain;       // fallen since the previous reading
    centi_mm rain_today; // since midnight
};

// "06:10 temp 12.34 C | hum 71.20 %RH | wind 3.42 m/s from 225 deg SW |
//  rain 0.40 mm"
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

// Log of the most recent readings, each with a sequence number so clients
// can fetch new readings incrementally. Kept apart from weather_station so
// it can be a zero-initialized global in .bss (it is large).
class weather_log
{
public:
    static constexpr size_t CAPACITY = 512; // 3.5 simulated days

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

// The weather station: a thermometer, hygrometer, anemometer, wind vane and
// tipping-bucket rain gauge measuring the simulated weather, with moving
// averages, and every reading appended to a weather_log.
class weather_station
{
public:
    static constexpr size_t AVERAGE_WINDOW = 8;
    static constexpr int32_t RAIN_GAUGE_TIP = 20;   // 0.2 mm per bucket tip
    static constexpr uint32_t READING_MINUTES = 10; // fast-forward step

    explicit constexpr weather_station(weather_log& log) : _log(log) {}

    // Advance the simulation to `now_ms` (real time) and take a reading.
    void update(uint64_t now_ms);

    // Jump the simulated clock to a time of day and take a fresh reading.
    void set_time_of_day(uint32_t minute);

    // Fast-forward: simulate `minutes` (rounded down to whole readings)
    // instantly, taking a reading every READING_MINUTES.
    void fast_forward(uint32_t minutes);

    bool has_reading() const
    {
        return _has_reading;
    }

    const weather_reading& latest() const
    {
        return _latest;
    }

    // Moving averages over the last AVERAGE_WINDOW readings. Wind direction is
    // averaged on the circle (350 and 10 degrees average to 0, not 180).
    weather_reading average() const;

    const weather_log& log() const
    {
        return _log;
    }

private:
    void take_reading();

    weather_model _model;
    sensor_model _thermometer{5, 1, -4000, 6000, 0x7e57'0001u}; // +/-0.05 C
    sensor_model _hygrometer{30, 10, 0, 10000, 0x7e57'0002u};   // +/-0.3 %
    sensor_model _anemometer{5, 10, 0, 6000, 0x7e57'0003u};     // 0.1 m/s
    sensor_model _wind_vane{2, 1, -720, 1080, 0x7e57'0004u};    // 1 deg

    using filter = util::moving_average<int32_t, AVERAGE_WINDOW, int64_t>;
    filter _temperature_avg;
    filter _humidity_avg;
    filter _wind_speed_avg;
    filter _wind_direction_avg; // of the unwrapped (continuous) direction
    int32_t _unwrapped_direction = 0;

    int32_t _rain_bucket = 0; // 0.01 mm collected but not yet tipped
    int32_t _rain_today = 0;  // 0.01 mm

    weather_log& _log;
    weather_reading _latest{};
    bool _has_reading = false;
    uint64_t _last_update_ms = 0;
};

} // namespace app
