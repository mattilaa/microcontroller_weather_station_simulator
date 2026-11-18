#pragma once

#include <stdint.h>

#include "util/random.hpp"

namespace app
{

// True physical conditions at one moment (what a perfect sensor would read).
struct weather_conditions
{
    int32_t temperature;    // 0.01 degC
    int32_t humidity;       // 0.01 %RH
    int32_t wind_speed;     // 0.01 m/s
    int32_t wind_direction; // degrees, 0..359 (where the wind comes from)
};

// Simulated weather. Deterministic daily patterns plus slowly varying random
// processes, so the data behaves like real weather instead of pure noise:
//
// - temperature: daily sine cycle (coldest ~03:00, warmest ~15:00) plus a
//   slow "weather front" offset that drifts over hours
// - humidity: falls when it gets warmer (relative humidity drops as air
//   warms), plus its own slow moisture drift
// - wind speed: mean-reverting around a daily mean (windier in the
//   afternoon), with occasional gusts that decay
// - wind direction: a prevailing direction that drifts slowly; the vane
//   swings more in light wind than in strong wind
//
// Simulated time runs SIM_SECONDS_PER_REAL_SECOND times faster than real time.
class weather_model
{
public:
    static constexpr uint32_t SIM_SECONDS_PER_REAL_SECOND = 600; // 10 min/s
    static constexpr uint32_t MINUTES_PER_DAY = 24 * 60;
    static constexpr uint32_t START_MINUTE = 6 * 60; // day 1 starts at 06:00

    // Advance simulated time by `real_ms` milliseconds of real time and
    // update the weather.
    void advance(uint32_t real_ms);

    // Jump to a time of day (0..1439) on the current day. Only the daily
    // patterns change; the slow random processes keep their state.
    void set_time_of_day(uint32_t minute);

    uint32_t day() const; // 1-based
    uint32_t minute_of_day() const;

    const weather_conditions& conditions() const
    {
        return _conditions;
    }

    // Deterministic daily patterns (exposed for tests and documentation).
    static int32_t daily_temperature(uint32_t minute_of_day);
    static int32_t daily_mean_wind(uint32_t minute_of_day);

private:
    void step_random_processes(uint32_t sim_seconds);
    void update_conditions();

    uint64_t _sim_seconds = START_MINUTE * 60;
    util::xorshift32 _rng{0x5eed'cafeu};

    int32_t _front = 0;            // temperature offset, 0.01 degC
    int32_t _moisture = 0;         // humidity offset, 0.01 %RH
    int32_t _sustained_wind = 300; // 0.01 m/s, mean-reverting
    int32_t _gust = 0;             // 0.01 m/s, decays every step
    int32_t _prevailing = 2250;    // 0.1 degrees (starts SW)
    int32_t _vane_swing = 0;       // degrees, turbulence of the moment

    weather_conditions _conditions{};
};

} // namespace app
