#include "app/weather_station.hpp"

#include "util/fixed_math.hpp"

namespace app
{

util::text_stream& operator<<(util::text_stream& out, const weather_reading& r)
{
    return out << r.time << " temp " << r.temperature << " | hum " << r.humidity
               << " | wind " << r.wind_speed << " from " << r.wind_direction
               << " | rain " << r.rain;
}

void weather_log::add(const weather_reading& reading)
{
    if(_entries.full())
    {
        entry oldest{};
        _entries.pop(oldest);
    }
    _entries.push({_next_sequence++, reading});
}

size_t weather_log::find(uint32_t sequence) const
{
    // Sequence numbers are consecutive, so the index is a subtraction.
    if(_entries.empty() || sequence <= _entries[0].sequence)
        return 0;
    const uint32_t offset = sequence - _entries[0].sequence;
    return offset < _entries.size() ? offset : _entries.size();
}

int32_t sensor_model::measure(int32_t true_value)
{
    const int32_t noisy = true_value + _rng.gaussian(_noise_sigma);
    return util::clamp(util::round_to(noisy, _resolution), _min, _max);
}

void weather_station::update(uint64_t now_ms)
{
    if(_has_reading)
        _model.advance(static_cast<uint32_t>(now_ms - _last_update_ms));
    else
        _model.advance(0); // compute the initial conditions
    _last_update_ms = now_ms;
    take_reading();
}

void weather_station::set_time_of_day(uint32_t minute)
{
    _model.set_time_of_day(minute);
    take_reading();
}

void weather_station::fast_forward(uint32_t minutes)
{
    for(uint32_t i = 0; i < minutes / READING_MINUTES; ++i)
    {
        _model.advance_simulated(READING_MINUTES * 60);
        take_reading();
    }
}

void weather_station::take_reading()
{
    const weather_conditions& truth = _model.conditions();

    // Tipping-bucket rain gauge: it reports rain only in whole 0.2 mm tips;
    // the rest stays in the bucket until the next tip.
    _rain_bucket += _model.take_rain();
    const int32_t rain = _rain_bucket / RAIN_GAUGE_TIP * RAIN_GAUGE_TIP;
    _rain_bucket -= rain;
    if(_has_reading && _model.day() != _latest.day)
        _rain_today = 0; // midnight
    _rain_today += rain;
    const int32_t direction =
        util::wrap(_wind_vane.measure(truth.wind_direction), 360);

    _latest = {
        .day = _model.day(),
        .time = {_model.minute_of_day()},
        .temperature = {_thermometer.measure(truth.temperature)},
        .humidity = {_hygrometer.measure(truth.humidity)},
        .wind_speed = {_anemometer.measure(truth.wind_speed)},
        .wind_direction = {direction},
        .rain = {rain},
        .rain_today = {_rain_today},
    };

    // Unwrap the direction so that averaging works across north: step by
    // the shortest turn (-180..180] from the previous direction.
    if(!_has_reading)
        _unwrapped_direction = direction;
    else
    {
        int32_t turn = direction - util::wrap(_unwrapped_direction, 360);
        if(turn > 180)
            turn -= 360;
        else if(turn <= -180)
            turn += 360;
        _unwrapped_direction += turn;
    }

    _temperature_avg.add(_latest.temperature.value);
    _humidity_avg.add(_latest.humidity.value);
    _wind_speed_avg.add(_latest.wind_speed.value);
    _wind_direction_avg.add(_unwrapped_direction);

    _log.add(_latest);
    _has_reading = true;
}

weather_reading weather_station::average() const
{
    return {
        .day = _latest.day,
        .time = _latest.time,
        .temperature = {_temperature_avg.average()},
        .humidity = {_humidity_avg.average()},
        .wind_speed = {_wind_speed_avg.average()},
        .wind_direction = {util::wrap(_wind_direction_avg.average(), 360)},
        .rain = _latest.rain,
        .rain_today = _latest.rain_today,
    };
}

} // namespace app
