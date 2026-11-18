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
};

// "06:10 temp 12.34 C | hum 71.20 %RH | wind 3.42 m/s from 225 deg SW"
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

// The weather station: a thermometer, hygrometer, anemometer and wind vane
// measuring the simulated weather, with moving averages and a short history.
class weather_station
{
public:
    static constexpr size_t HISTORY = 8;

    // Advance the simulation to `now_ms` (real time) and take a reading.
    void update(uint64_t now_ms);

    // Jump the simulated clock to a time of day and take a fresh reading.
    void set_time_of_day(uint32_t minute);

    bool has_reading() const
    {
        return _has_reading;
    }

    const weather_reading& latest() const
    {
        return _latest;
    }

    // Moving averages over the last HISTORY readings. Wind direction is
    // averaged on the circle (350 and 10 degrees average to 0, not 180).
    weather_reading average() const;

    const util::ring_buffer<weather_reading, HISTORY>& history() const
    {
        return _history;
    }

private:
    void take_reading();

    weather_model _model;
    sensor_model _thermometer{5, 1, -4000, 6000, 0x7e57'0001u}; // +/-0.05 C
    sensor_model _hygrometer{30, 10, 0, 10000, 0x7e57'0002u};   // +/-0.3 %
    sensor_model _anemometer{5, 10, 0, 6000, 0x7e57'0003u};     // 0.1 m/s
    sensor_model _wind_vane{2, 1, -720, 1080, 0x7e57'0004u};    // 1 deg

    using filter = util::moving_average<int32_t, HISTORY, int64_t>;
    filter _temperature_avg;
    filter _humidity_avg;
    filter _wind_speed_avg;
    filter _wind_direction_avg; // of the unwrapped (continuous) direction
    int32_t _unwrapped_direction = 0;

    util::ring_buffer<weather_reading, HISTORY> _history;
    weather_reading _latest{};
    bool _has_reading = false;
    uint64_t _last_update_ms = 0;
};

} // namespace app
