#include "app/weather_station.hpp"

#include "util/fixed_math.hpp"

namespace app
{

util::text_stream& operator<<(util::text_stream& out, const weather_reading& r)
{
    return out << r.time << " temp " << r.temperature << " | hum " << r.humidity
               << " | wind " << r.wind_speed << " from " << r.wind_direction
               << " | rain " << r.rain << " | cloud " << r.cloud_cover;
}

int32_t sensor_model::measure(int32_t true_value)
{
    const int32_t noisy = true_value + _rng.gaussian(_noise_sigma);
    return util::clamp(util::round_to(noisy, _resolution), _min, _max);
}

// ---- weather_log
// ----------------------------------------------------------------

// The packed record is what keeps the log inside the RAM budget.
static_assert(sizeof(weather_log::entry) == 40);

void derive_cloud_levels(cloud_layers& c)
{
    // Amounts are whole percent here; the model works in per mille.
    c.stratus_top = {
        weather_model::stratus_top(c.lcl.value, c.stratus.value * 10)};
    c.stratocumulus_top = {weather_model::stratocumulus_top(
        c.lcl.value,
        c.stratocumulus.value * 10)};
    c.nimbostratus_top = c.altostratus_top;
    const weather_model::levels ac = weather_model::altocumulus_levels(
        c.altostratus_base.value,
        c.altostratus_top.value,
        c.altocumulus.value * 10);
    c.altocumulus_base = {ac.base};
    c.altocumulus_top = {ac.top};
}

namespace
{
template <typename T>
T pack(int32_t value, int32_t lo, int32_t hi)
{
    return static_cast<T>(util::clamp(value, lo, hi));
}
} // namespace

void weather_log::add(const weather_reading& r)
{
    if(_entries.full())
    {
        entry oldest{};
        _entries.pop(oldest);
    }
    _entries.push({
        .sequence = _next_sequence++,
        .day = pack<uint16_t>(static_cast<int32_t>(r.day), 0, 65535),
        .minute_of_day = pack<
            uint16_t>(static_cast<int32_t>(r.time.second_of_day / 60), 0, 1439),
        .temperature = pack<int16_t>(r.temperature.value, -32768, 32767),
        .humidity = pack<uint16_t>(r.humidity.value, 0, 10000),
        .wind_speed = pack<uint16_t>(r.wind_speed.value, 0, 65535),
        .wind_direction = pack<uint16_t>(r.wind_direction.value, 0, 359),
        .rain = pack<uint16_t>(r.rain.value, 0, 65535),
        .lcl = pack<uint16_t>(r.clouds.lcl.value, 0, 1100),
        .cumulus_top = pack<uint16_t>(r.clouds.cumulus_top.value, 0, 1100),
        .cirrus_base = pack<uint16_t>(r.clouds.cirrus_base.value, 0, 1100),
        .cirrus_top = pack<uint16_t>(r.clouds.cirrus_top.value, 0, 1100),
        .altostratus_base =
            pack<uint16_t>(r.clouds.altostratus_base.value, 0, 1100),
        .altostratus_top =
            pack<uint16_t>(r.clouds.altostratus_top.value, 0, 1100),
        .cloud_cover = pack<uint8_t>(r.cloud_cover.value, 0, 100),
        .cirrus = pack<uint8_t>(r.clouds.cirrus.value, 0, 100),
        .altostratus = pack<uint8_t>(r.clouds.altostratus.value, 0, 100),
        .cumulus = pack<uint8_t>(r.clouds.cumulus.value, 0, 100),
        .cumulonimbus = pack<uint8_t>(r.clouds.cumulonimbus.value, 0, 100),
        .stratus = pack<uint8_t>(r.clouds.stratus.value, 0, 100),
        .stratocumulus = pack<uint8_t>(r.clouds.stratocumulus.value, 0, 100),
        .nimbostratus = pack<uint8_t>(r.clouds.nimbostratus.value, 0, 100),
        .altocumulus = pack<uint8_t>(r.clouds.altocumulus.value, 0, 100),
    });
}

weather_reading weather_log::entry::reading() const
{
    weather_reading r{
        .day = day,
        .time = {static_cast<uint32_t>(minute_of_day) * 60},
        .temperature = {temperature},
        .humidity = {humidity},
        .wind_speed = {wind_speed},
        .wind_direction = {wind_direction},
        .rain = {rain},
        .rain_today = {0},
        .cloud_cover = {cloud_cover},
        .clouds =
            {
                .cirrus = {cirrus},
                .altostratus = {altostratus},
                .cumulus = {cumulus},
                .cumulonimbus = {cumulonimbus},
                .lcl = {lcl},
                .cumulus_top = {cumulus_top},
                .cirrus_base = {cirrus_base},
                .cirrus_top = {cirrus_top},
                .altostratus_base = {altostratus_base},
                .altostratus_top = {altostratus_top},
                .stratus = {stratus},
                .stratocumulus = {stratocumulus},
                .nimbostratus = {nimbostratus},
                .altocumulus = {altocumulus},
            },
    };
    derive_cloud_levels(r.clouds);
    return r;
}

size_t weather_log::find(uint32_t sequence) const
{
    // Sequence numbers are consecutive, so the index is a subtraction.
    if(_entries.empty() || sequence <= _entries[0].sequence)
        return 0;
    const uint32_t offset = sequence - _entries[0].sequence;
    return offset < _entries.size() ? offset : _entries.size();
}

// ---- weather_station
// ------------------------------------------------------------

void weather_station::set_speed(uint32_t speed)
{
    _speed = speed < 1 ? 1 : (speed > MAX_SPEED ? MAX_SPEED : speed);
}

void weather_station::update(uint64_t now_ms)
{
    _update_rain = 0;
    if(!_has_reading)
    {
        _model.advance_simulated(0); // compute the initial conditions
        _last_update_ms = now_ms;
        sample(0);
        return;
    }
    _unsimulated_ms += (now_ms - _last_update_ms) * _speed;
    _last_update_ms = now_ms;
    const uint64_t sim_seconds = _unsimulated_ms / 1000;
    _unsimulated_ms %= 1000;
    simulate(sim_seconds);
}

void weather_station::fast_forward(uint32_t minutes)
{
    _update_rain = 0;
    simulate(static_cast<uint64_t>(minutes) * 60);
}

void weather_station::set_time_of_day(uint32_t minute)
{
    _model.set_time_of_day(minute);
    // Start a fresh interval: the old one doesn't fit the new clock.
    reset_interval();
    _update_rain = 0;
    sample(0);
}

void weather_station::simulate(uint64_t sim_seconds)
{
    while(sim_seconds > 0)
    {
        // Step to the next sample: at most a minute, and never past the end
        // of the current 10-minute interval.
        const auto into_interval =
            static_cast<uint32_t>(_model.simulated_seconds() % RECORD_SECONDS);
        uint64_t chunk = RECORD_SECONDS - into_interval;
        if(chunk > MAX_SAMPLE_SECONDS)
            chunk = MAX_SAMPLE_SECONDS;
        if(chunk > sim_seconds)
            chunk = sim_seconds;

        _model.advance_simulated(static_cast<uint32_t>(chunk));
        sample(static_cast<uint32_t>(chunk));
        sim_seconds -= chunk;

        if(_model.simulated_seconds() % RECORD_SECONDS == 0)
            close_record();
    }
}

void weather_station::sample(uint32_t weight_seconds)
{
    const weather_conditions& truth = _model.conditions();
    const int32_t direction =
        util::wrap(_wind_vane.measure(truth.wind_direction), 360);

    // Tipping-bucket rain gauge: it reports rain only in whole 0.2 mm tips;
    // the rest stays in the bucket until the next tip.
    _rain_bucket += _model.take_rain();
    const int32_t tipped = _rain_bucket / RAIN_GAUGE_TIP * RAIN_GAUGE_TIP;
    _rain_bucket -= tipped;
    _interval_rain += tipped;
    _update_rain += tipped;
    _rain_today += tipped;

    _latest = {
        .day = _model.day(),
        .time = {_model.second_of_day()},
        .temperature = {_thermometer.measure(truth.temperature)},
        .humidity = {_hygrometer.measure(truth.humidity)},
        .wind_speed = {_anemometer.measure(truth.wind_speed)},
        .wind_direction = {direction},
        .rain = {_update_rain},
        .rain_today = {_rain_today},
        .cloud_cover = {_sky_imager.measure(truth.cloud_cover / 10)},
        .clouds =
            {
                .cirrus = {_cloud_sensor.measure(truth.cirrus / 10)},
                .altostratus = {_cloud_sensor.measure(truth.altostratus / 10)},
                .cumulus = {_cloud_sensor.measure(truth.cumulus / 10)},
                .cumulonimbus = {_cloud_sensor.measure(
                    truth.cumulonimbus / 10)},
                .lcl = {_ceilometer.measure(truth.lcl)},
                .cumulus_top = {_ceilometer.measure(truth.cumulus_top)},
                .cirrus_base = {_ceilometer.measure(truth.cirrus_base)},
                .cirrus_top = {_ceilometer.measure(truth.cirrus_top)},
                .altostratus_base = {_ceilometer.measure(
                    truth.altostratus_base)},
                .altostratus_top = {_ceilometer.measure(truth.altostratus_top)},
                .stratus = {_cloud_sensor.measure(truth.stratus / 10)},
                .stratocumulus = {_cloud_sensor.measure(
                    truth.stratocumulus / 10)},
                .nimbostratus = {_cloud_sensor.measure(
                    truth.nimbostratus / 10)},
                .altocumulus = {_cloud_sensor.measure(truth.altocumulus / 10)},
            },
    };
    derive_cloud_levels(_latest.clouds);

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

    // An instantaneous sample (weight 0) still counts when it's the first.
    const uint32_t weight = weight_seconds > 0 ? weight_seconds
                            : _sum_weight == 0 ? 1
                                               : 0;
    _sum_temperature +=
        static_cast<int64_t>(_latest.temperature.value) * weight;
    _sum_humidity += static_cast<int64_t>(_latest.humidity.value) * weight;
    _sum_wind_speed += static_cast<int64_t>(_latest.wind_speed.value) * weight;
    _sum_cloud_cover +=
        static_cast<int64_t>(_latest.cloud_cover.value) * weight;
    const cloud_layers& c = _latest.clouds;
    _sum_cirrus += static_cast<int64_t>(c.cirrus.value) * weight;
    _sum_altostratus += static_cast<int64_t>(c.altostratus.value) * weight;
    _sum_cumulus += static_cast<int64_t>(c.cumulus.value) * weight;
    _sum_cumulonimbus += static_cast<int64_t>(c.cumulonimbus.value) * weight;
    _sum_lcl += static_cast<int64_t>(c.lcl.value) * weight;
    _sum_cumulus_top += static_cast<int64_t>(c.cumulus_top.value) * weight;
    _sum_cirrus_base += static_cast<int64_t>(c.cirrus_base.value) * weight;
    _sum_cirrus_top += static_cast<int64_t>(c.cirrus_top.value) * weight;
    _sum_altostratus_base +=
        static_cast<int64_t>(c.altostratus_base.value) * weight;
    _sum_altostratus_top +=
        static_cast<int64_t>(c.altostratus_top.value) * weight;
    _sum_stratus += static_cast<int64_t>(c.stratus.value) * weight;
    _sum_stratocumulus += static_cast<int64_t>(c.stratocumulus.value) * weight;
    _sum_nimbostratus += static_cast<int64_t>(c.nimbostratus.value) * weight;
    _sum_altocumulus += static_cast<int64_t>(c.altocumulus.value) * weight;
    _sum_direction += static_cast<int64_t>(_unwrapped_direction) * weight;
    _sum_weight += weight;
    _has_reading = true;
}

weather_reading weather_station::interval_mean() const
{
    if(_sum_weight == 0)
        return _latest;
    weather_reading result = mean_of_interval();
    derive_cloud_levels(result.clouds);
    return result;
}

weather_reading weather_station::mean_of_interval() const
{
    const auto mean = [this](int64_t sum)
    { return static_cast<int32_t>(sum / _sum_weight); };
    return {
        .day = _latest.day,
        .time = _latest.time,
        .temperature = {mean(_sum_temperature)},
        .humidity = {mean(_sum_humidity)},
        .wind_speed = {mean(_sum_wind_speed)},
        .wind_direction = {util::wrap(mean(_sum_direction), 360)},
        .rain = {_interval_rain},
        .rain_today = {_rain_today},
        .cloud_cover = {mean(_sum_cloud_cover)},
        .clouds =
            {
                .cirrus = {mean(_sum_cirrus)},
                .altostratus = {mean(_sum_altostratus)},
                .cumulus = {mean(_sum_cumulus)},
                .cumulonimbus = {mean(_sum_cumulonimbus)},
                .lcl = {mean(_sum_lcl)},
                .cumulus_top = {mean(_sum_cumulus_top)},
                .cirrus_base = {mean(_sum_cirrus_base)},
                .cirrus_top = {mean(_sum_cirrus_top)},
                .altostratus_base = {mean(_sum_altostratus_base)},
                .altostratus_top = {mean(_sum_altostratus_top)},
                .stratus = {mean(_sum_stratus)},
                .stratocumulus = {mean(_sum_stratocumulus)},
                .nimbostratus = {mean(_sum_nimbostratus)},
                .altocumulus = {mean(_sum_altocumulus)},
            },
    };
}

void weather_station::close_record()
{
    _log.add(interval_mean());

    // The record ending at midnight still belongs to the old day's total.
    if(_model.second_of_day() == 0)
        _rain_today = 0;

    reset_interval();
}

void weather_station::reset_interval()
{
    _sum_temperature = _sum_humidity = _sum_wind_speed = _sum_direction = 0;
    _sum_cloud_cover = _sum_cirrus = _sum_altostratus = _sum_cumulus = 0;
    _sum_cumulonimbus = _sum_lcl = _sum_cumulus_top = 0;
    _sum_cirrus_base = _sum_cirrus_top = 0;
    _sum_altostratus_base = _sum_altostratus_top = 0;
    _sum_stratus = _sum_stratocumulus = _sum_nimbostratus = 0;
    _sum_altocumulus = 0;
    _sum_weight = 0;
    _interval_rain = 0;
}

} // namespace app
