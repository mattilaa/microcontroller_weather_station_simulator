#include "app/weather_model.hpp"

#include "util/fixed_math.hpp"

namespace app
{

namespace
{
// Daily cycle phase: 360 degrees per day, with the sine peaking at 15:00 and
// bottoming out at 03:00.
int32_t daily_phase_degrees(uint32_t minute_of_day)
{
    constexpr int32_t PEAK_MINUTE = 15 * 60;
    // sin() peaks at 90 degrees; one degree is four minutes.
    return (static_cast<int32_t>(minute_of_day) - PEAK_MINUTE) / 4 + 90;
}

// One mean-reversion step of an Ornstein-Uhlenbeck-like process:
// move toward `target` by dt/tau of the distance, then add noise.
int32_t revert(
    int32_t value,
    int32_t target,
    uint32_t dt_seconds,
    uint32_t tau_seconds,
    int32_t noise)
{
    const int64_t pull = static_cast<int64_t>(target - value) * dt_seconds /
                         (dt_seconds > tau_seconds ? dt_seconds : tau_seconds);
    return value + static_cast<int32_t>(pull) + noise;
}
} // namespace

int32_t weather_model::daily_temperature(uint32_t minute_of_day)
{
    constexpr int32_t MEAN = 1500;     // 15.00 C
    constexpr int32_t AMPLITUDE = 800; // +/- 8.00 C
    return MEAN + AMPLITUDE *
                      util::sin_deg(daily_phase_degrees(minute_of_day)) /
                      util::SIN_SCALE;
}

int32_t weather_model::daily_mean_wind(uint32_t minute_of_day)
{
    constexpr int32_t MEAN = 300;      // 3.00 m/s
    constexpr int32_t AMPLITUDE = 200; // +/- 2.00 m/s
    return MEAN + AMPLITUDE *
                      util::sin_deg(daily_phase_degrees(minute_of_day)) /
                      util::SIN_SCALE;
}

uint32_t weather_model::day() const
{
    return static_cast<uint32_t>(_sim_seconds / (MINUTES_PER_DAY * 60)) + 1;
}

uint32_t weather_model::minute_of_day() const
{
    return static_cast<uint32_t>(_sim_seconds / 60 % MINUTES_PER_DAY);
}

void weather_model::advance(uint32_t real_ms)
{
    const uint32_t sim_seconds = static_cast<uint32_t>(
        static_cast<uint64_t>(real_ms) * SIM_SECONDS_PER_REAL_SECOND / 1000);
    _sim_seconds += sim_seconds;
    if(sim_seconds > 0)
        step_random_processes(sim_seconds);
    update_conditions();
}

void weather_model::set_time_of_day(uint32_t minute)
{
    const uint64_t day_start =
        _sim_seconds / (MINUTES_PER_DAY * 60) * (MINUTES_PER_DAY * 60);
    _sim_seconds = day_start + (minute % MINUTES_PER_DAY) * 60;
    update_conditions();
}

void weather_model::step_random_processes(uint32_t dt)
{
    // Weather fronts: +/- 3 C offset that wanders over hours.
    _front = util::clamp(
        revert(_front, 0, dt, 6 * 3600, _rng.gaussian(30)),
        -300,
        300);

    // Moisture: +/- 10 %RH offset, a little faster than fronts.
    _moisture = util::clamp(
        revert(_moisture, 0, dt, 3 * 3600, _rng.gaussian(100)),
        -1000,
        1000);

    // Sustained wind follows the daily mean with a 30 min time constant.
    _sustained_wind = util::clamp(
        revert(
            _sustained_wind,
            daily_mean_wind(minute_of_day()),
            dt,
            30 * 60,
            _rng.gaussian(40)),
        0,
        2000);

    // Gusts: decay by 40 % per step; 5 % chance of a new one each step.
    _gust = _gust * 6 / 10;
    if(_rng.below(100) < 5)
        _gust += _rng.between(300, 800);

    // Prevailing direction drifts ~5 degrees per step, pulled back toward
    // SW over half a day.
    _prevailing = revert(_prevailing, 2250, dt, 12 * 3600, _rng.gaussian(50));

    // Vane swing: about 30 degrees in calm air, a few degrees in strong wind.
    const int32_t speed = _sustained_wind + _gust;
    // sigma = 30 deg * 1 m/s / (speed + 1 m/s): 30 deg calm, ~7 deg at 3 m/s.
    _vane_swing = _rng.gaussian(3000 / (speed + 100));
}

void weather_model::update_conditions()
{
    const uint32_t minute = minute_of_day();
    const int32_t temperature = daily_temperature(minute) + _front;

    // Relative humidity drops ~3 %RH per degree of warming above the mean.
    const int32_t humidity = 6500 - 3 * (temperature - 1500) + _moisture;

    _conditions = {
        .temperature = temperature,
        .humidity = util::clamp(humidity, 1500, 10000),
        .wind_speed = util::clamp(_sustained_wind + _gust, 0, 4000),
        .wind_direction = util::wrap(_prevailing / 10 + _vane_swing, 360),
    };
}

} // namespace app
