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

// The random processes are kept FINE times finer than their public units, so
// a one-minute step's tiny pull toward the mean doesn't round away to 0.
constexpr int32_t FINE = weather_model::FINE;

// Noise per one-minute step. A random walk's spread grows with the square
// root of time, so these are the 10-minute values divided by sqrt(10).
constexpr int32_t FRONT_NOISE = 9 * FINE;       // 0.01 C
constexpr int32_t MOISTURE_NOISE = 32 * FINE;   // 0.01 %RH
constexpr int32_t CLOUD_NOISE = 6 * FINE;       // per mille
constexpr int32_t WIND_NOISE = 13 * FINE;       // 0.01 m/s
constexpr int32_t PREVAILING_NOISE = 16 * FINE; // 0.1 degrees
} // namespace

int32_t weather_model::daily_temperature(uint32_t minute_of_day) const
{
    return _scenario->temperature_mean +
           _scenario->temperature_swing *
               util::sin_deg(daily_phase_degrees(minute_of_day)) /
               util::SIN_SCALE;
}

int32_t weather_model::daily_convection(uint32_t minute_of_day)
{
    // Positive half of the daily cycle (09:00..21:00), peaking at 15:00.
    const int32_t s = util::sin_deg(daily_phase_degrees(minute_of_day));
    return s > 0 ? s / 10 : 0;
}

int32_t weather_model::lcl_hpa(int32_t humidity, int32_t surface_pressure)
{
    // Height: 125 m * (100 - RH) / 5 = 25 m per %RH below saturation; the
    // standard atmosphere scaled to the actual surface pressure.
    const int32_t metres = (10000 - util::clamp(humidity, 0, 10000)) / 4;
    return util::standard_pressure_hpa(metres) * surface_pressure /
           STANDARD_PRESSURE;
}

int32_t weather_model::stratus_top(int32_t lcl, int32_t amount)
{
    return lcl - 25 - 40 * util::clamp(amount, 0, 1000) / 1000; // 25..65 hPa
}

int32_t weather_model::stratocumulus_top(int32_t lcl, int32_t amount)
{
    return lcl - 60 - 90 * util::clamp(amount, 0, 1000) / 1000; // 60..150 hPa
}

weather_model::levels weather_model::altocumulus_levels(
    int32_t altostratus_base,
    int32_t altostratus_top,
    int32_t amount)
{
    const int32_t centre = (altostratus_base + altostratus_top) / 2 - 15;
    const int32_t half = 15 + 50 * util::clamp(amount, 0, 1000) / 1000;
    return {.base = centre + half, .top = centre - half};
}

int32_t weather_model::daily_mean_wind(uint32_t minute_of_day) const
{
    return _scenario->wind_mean +
           _scenario->wind_swing *
               util::sin_deg(daily_phase_degrees(minute_of_day)) /
               util::SIN_SCALE;
}

int32_t weather_model::cloudiness() const
{
    // The slow cloud process, plus the cloud afternoon convection builds in
    // some scenarios. It follows the convection and the cell process, so
    // thunderstorm cells tower up and collapse within the hour, mostly in
    // the afternoon.
    const int32_t convective =
        _scenario->convective_cloud * daily_convection(minute_of_day()) / 1000 *
        _scenario->convection / 1000 * (_cell / FINE) / 1000;
    return util::clamp(_cloud / FINE + convective, 0, 1000);
}

void weather_model::set_scenario(const weather_scenario& scenario)
{
    _scenario = &scenario;
    _scenario_start = _sim_seconds;
    _front = _moisture = _wetness = _gust = 0;
    _cloud_trend = _cloud = scenario.cloud_mean * FINE;
    _sustained_wind = daily_mean_wind(minute_of_day()) * FINE;
    _prevailing = scenario.prevailing * FINE;
    _cirrus = scenario.cirrus_mean * FINE;
    _pressure = scenario.pressure_mean * FINE;
    update_conditions();
}

uint32_t weather_model::day() const
{
    return static_cast<uint32_t>(_sim_seconds / SECONDS_PER_DAY) + 1;
}

uint32_t weather_model::second_of_day() const
{
    return static_cast<uint32_t>(_sim_seconds % SECONDS_PER_DAY);
}

uint32_t weather_model::minute_of_day() const
{
    return second_of_day() / 60;
}

void weather_model::advance_simulated(uint32_t sim_seconds)
{
    _sim_seconds += sim_seconds;
    _unstepped_seconds += sim_seconds;
    while(_unstepped_seconds >= STEP_SECONDS)
    {
        _unstepped_seconds -= STEP_SECONDS;
        step(STEP_SECONDS);
    }
    update_conditions();
}

int32_t weather_model::take_rain()
{
    // _rain_fallen is in (0.01 mm/h) * s; keep the remainder for next time.
    const int64_t amount = _rain_fallen / 3600;
    _rain_fallen -= amount * 3600;
    return static_cast<int32_t>(amount);
}

void weather_model::set_time_of_day(uint32_t minute)
{
    const uint64_t day_start = _sim_seconds / SECONDS_PER_DAY * SECONDS_PER_DAY;
    _sim_seconds = day_start + (minute % MINUTES_PER_DAY) * 60;
    update_conditions();
}

void weather_model::step(uint32_t dt)
{
    // Weather fronts: +/- 3 C offset that wanders over hours.
    _front = util::clamp(
        revert(_front, 0, dt, 6 * 3600, _rng.gaussian(FRONT_NOISE)),
        -300 * FINE,
        300 * FINE);

    // Moisture: +/- 10 %RH offset, a little faster than fronts.
    _moisture = util::clamp(
        revert(_moisture, 0, dt, 3 * 3600, _rng.gaussian(MOISTURE_NOISE)),
        -1000 * FINE,
        1000 * FINE);

    // Cloudiness follows a slowly wandering trend (hours to build up and
    // clear), so showers ramp up and die out instead of flickering. It
    // spends roughly a seventh of the time above the rain threshold.
    _cloud_trend = util::clamp(
        revert(
            _cloud_trend,
            _scenario->cloud_mean * FINE,
            dt,
            6 * 3600,
            _rng.gaussian(_scenario->cloud_trend_noise * FINE)),
        0,
        1000 * FINE);
    _cloud = util::clamp(
        revert(_cloud, _cloud_trend, dt, 3600, _rng.gaussian(CLOUD_NOISE)),
        0,
        1000 * FINE);

    // Wetness builds up within half an hour of rain and dries over hours.
    const bool raining = cloudiness() > _scenario->rain_threshold;
    _wetness = raining ? revert(_wetness, 1000 * FINE, dt, 30 * 60, 0)
                       : revert(_wetness, 0, dt, 2 * 3600, 0);

    // Sustained wind follows the daily mean with a 30 min time constant.
    _sustained_wind = util::clamp(
        revert(
            _sustained_wind,
            daily_mean_wind(minute_of_day()) * FINE,
            dt,
            30 * 60,
            _rng.gaussian(WIND_NOISE)),
        0,
        2000 * FINE);

    // Gusts: halve every 10 minutes (x0.933 per minute); a new gust with
    // the scenario's chance per minute (default 0.6 %, 1-3 m/s).
    _gust = static_cast<int32_t>(static_cast<int64_t>(_gust) * 933 / 1000);
    if(_rng.below(1000) < static_cast<uint32_t>(_scenario->gust_chance))
        _gust += _rng.between(_scenario->gust_min, _scenario->gust_max) * FINE;

    // Prevailing direction drifts a few degrees per 10 minutes, pulled back
    // toward the scenario's direction over half a day. That direction turns
    // clockwise at the scenario's veering rate (a front passing).
    const int64_t veered =
        static_cast<int64_t>(_scenario->veering) * FINE *
        static_cast<int64_t>(_sim_seconds - _scenario_start) / 3600;
    _prevailing = revert(
        _prevailing,
        _scenario->prevailing * FINE + static_cast<int32_t>(veered),
        dt,
        12 * 3600,
        _rng.gaussian(PREVAILING_NOISE));

    // Vane swing: about 30 degrees in calm air, a few degrees in strong wind.
    const int32_t speed = (_sustained_wind + _gust) / FINE;
    // sigma = 30 deg * 1 m/s / (speed + 1 m/s): 30 deg calm, ~7 deg at 3 m/s.
    _vane_swing = _rng.gaussian(3000 / (speed + 100));

    // High cloud: its own slow drift around a quarter of the sky.
    constexpr int32_t CIRRUS_NOISE = 16 * FINE;
    _cirrus = util::clamp(
        revert(
            _cirrus,
            _scenario->cirrus_mean * FINE,
            dt,
            8 * 3600,
            _cirrus_rng.gaussian(CIRRUS_NOISE)),
        0,
        1000 * FINE);

    // The high and middle layers' centre levels drift over hours.
    _cirrus_level = util::clamp(
        revert(
            _cirrus_level,
            270 * FINE,
            dt,
            6 * 3600,
            _level_rng.gaussian(4 * FINE)),
        230 * FINE,
        330 * FINE);
    _altostratus_level = util::clamp(
        revert(
            _altostratus_level,
            580 * FINE,
            dt,
            4 * 3600,
            _level_rng.gaussian(7 * FINE)),
        500 * FINE,
        660 * FINE);

    // Surface pressure drifts around the scenario's mean (a few hPa); a
    // storm's low deepens over 12 hours and fills over the next 12.
    const auto since_start = static_cast<int32_t>(
        (_sim_seconds - _scenario_start) % SECONDS_PER_DAY);
    const int32_t dip = _scenario->pressure_dip *
                        (util::SIN_SCALE - util::cos_deg(since_start / 240)) /
                        (2 * util::SIN_SCALE);
    _pressure = util::clamp(
        revert(
            _pressure,
            (_scenario->pressure_mean - dip) * FINE,
            dt,
            3 * 3600,
            _pressure_rng.gaussian(3 * FINE)),
        8700 * FINE,
        10850 * FINE);

    // Gust factor for the coming minute: peaks 10..40 % above the wind of
    // the moment. A 10-minute record keeps the highest of ten such peaks,
    // which comes out around 1.4..1.6 times the record's mean wind, as
    // measured over land.
    _gust_factor = _gust_rng.between(1100, 1400);

    // Convective cells: mean-reverting over ~40 minutes, so they come and go.
    _cell = util::clamp(
        revert(_cell, 450 * FINE, dt, 40 * 60, _cell_rng.gaussian(67 * FINE)),
        0,
        1000 * FINE);

    // Rain falls at the step's rate (computed from the new cloudiness).
    update_conditions();
    _rain_fallen += static_cast<int64_t>(_conditions.rain_rate) * dt;
}

void weather_model::update_conditions()
{
    const uint32_t minute = minute_of_day();
    const int32_t cloud = cloudiness();

    const weather_scenario& sc = *_scenario;

    // Rain: 0 below the threshold, then rain_per_cloud per mille of cloud
    // above it (default 0.015 mm/h per step: up to ~5 mm/h).
    const int32_t rain_rate =
        cloud > sc.rain_threshold
            ? (cloud - sc.rain_threshold) * sc.rain_per_cloud / 1000
            : 0;

    // Clouds flatten the daily swing (to 75 % under full overcast) and rain
    // cools the air (1 C at 4 mm/h).
    const int32_t clear_sky_swing =
        daily_temperature(minute) - sc.temperature_mean;
    const int32_t temperature = sc.temperature_mean +
                                clear_sky_swing * (1000 - cloud / 4) / 1000 +
                                _front / FINE - rain_rate / 4;

    // Relative humidity drops with warming above the mean (default ~3 %RH
    // per degree), rises with cloudiness (+/- 7 %RH) and with wetness (up
    // to +25 %RH).
    const int32_t humidity =
        sc.humidity_base -
        sc.humidity_per_degree * (temperature - sc.temperature_mean) +
        _moisture / FINE + (cloud - sc.cloud_mean) * 2 +
        _wetness / FINE * 5 / 2;

    // Cloud layers. The cloudiness that makes rain is low and middle cloud,
    // split into types (the total, and so the rain, doesn't change):
    // - heavy rain in strong convection is cumulonimbus, from the cloud
    //   base right up to the tropopause
    // - steady rain without convection falls from nimbostratus
    // - of the rest, daytime convection heaps a share into cumulus
    // - the remaining stratiform cloud is half middle cloud and half low
    //   cloud. Low: stratus in very humid air, stratocumulus otherwise.
    //   Middle: altostratus sheet, with a share of lumpy altocumulus that
    //   grows in the daytime
    const int32_t relative_humidity = util::clamp(humidity, 1500, 10000);
    const int32_t convection = daily_convection(minute) * sc.convection / 1000;
    int32_t cumulonimbus = 0;
    if(cloud > sc.storm_cloud && convection > sc.storm_convection)
        cumulonimbus = util::clamp(
            (cloud - sc.storm_cloud) * 3 * convection / 1000,
            0,
            cloud);
    const int32_t layered = cloud - cumulonimbus;
    const int32_t nimbostratus =
        rain_rate > 0 ? util::clamp((cloud - 600) * 3, 0, layered) *
                            (1000 - convection) / 1000
                      : 0;
    const int32_t rest = layered - nimbostratus;
    const int32_t cumulus = rest * convection / 1000;
    const int32_t stratiform = rest - cumulus;
    const int32_t low = stratiform / 2;
    // 0 at 75 %RH, 1000 when saturated: how much of the low cloud is stratus.
    const int32_t saturation =
        util::clamp((relative_humidity - 7500) * 1000 / 2500, 0, 1000);
    const int32_t stratus = low * saturation / 1000;
    const int32_t stratocumulus = low - stratus;
    const int32_t middle = stratiform - low;
    const int32_t altocumulus = middle * (300 + 400 * convection / 1000) / 1000;
    const int32_t altostratus = middle - altocumulus;
    const int32_t cirrus = _cirrus / FINE;

    const int32_t pressure = _pressure / FINE;
    const int32_t lcl = lcl_hpa(relative_humidity, pressure);
    // Cumulus towers deepen with convection: 80..350 hPa above the base.
    const int32_t cumulus_top =
        util::clamp(lcl - 80 - 270 * convection / 1000, 450, lcl);

    // High and middle layers: centred on their drifting levels, thicker the
    // more cloud there is.
    const int32_t cirrus_half = 15 + 60 * cirrus / 1000;
    const int32_t cirrus_level = _cirrus_level / FINE;
    const int32_t cirrus_base =
        util::clamp(cirrus_level + cirrus_half, cirrus_level, CIRRUS_LOWEST);
    const int32_t cirrus_top =
        util::clamp(cirrus_level - cirrus_half, CIRRUS_HIGHEST, cirrus_level);
    const int32_t altostratus_half = 20 + 110 * altostratus / 1000;
    const int32_t altostratus_level = _altostratus_level / FINE;
    const int32_t altostratus_base = util::clamp(
        altostratus_level + altostratus_half,
        altostratus_level,
        util::clamp(lcl - 20, altostratus_level, ALTOSTRATUS_LOWEST));
    const int32_t altostratus_top = util::clamp(
        altostratus_level - altostratus_half,
        ALTOSTRATUS_HIGHEST,
        altostratus_level);

    // The sky imager sees all layers; thin cirrus counts half.
    const int32_t sky = cloud + cirrus / 2 * (1000 - cloud) / 1000;

    _conditions = {
        .temperature = temperature,
        .humidity = relative_humidity,
        .wind_speed = util::clamp((_sustained_wind + _gust) / FINE, 0, 4000),
        .wind_gust = util::clamp(
            (_sustained_wind + _gust) / FINE * _gust_factor / 1000,
            0,
            6000),
        .wind_direction =
            util::wrap(_prevailing / FINE / 10 + _vane_swing, 360),
        .rain_rate = rain_rate,
        .pressure = pressure,
        .cloud_cover = sky,
        .cirrus = cirrus,
        .altostratus = altostratus,
        .cumulus = cumulus,
        .cumulonimbus = cumulonimbus,
        .lcl = lcl,
        .cumulus_top = cumulus_top,
        .cirrus_base = cirrus_base,
        .cirrus_top = cirrus_top,
        .altostratus_base = altostratus_base,
        .altostratus_top = altostratus_top,
        .stratus = stratus,
        .stratocumulus = stratocumulus,
        .nimbostratus = nimbostratus,
        .altocumulus = altocumulus,
        .stratus_top = stratus_top(lcl, stratus),
        .stratocumulus_top = stratocumulus_top(lcl, stratocumulus),
        .nimbostratus_top = altostratus_top,
        .altocumulus_base =
            altocumulus_levels(altostratus_base, altostratus_top, altocumulus)
                .base,
        .altocumulus_top =
            altocumulus_levels(altostratus_base, altostratus_top, altocumulus)
                .top,
    };
}

} // namespace app
