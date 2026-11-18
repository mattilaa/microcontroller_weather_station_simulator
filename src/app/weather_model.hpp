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
    int32_t rain_rate;      // 0.01 mm/h
    int32_t cloud_cover;    // 0.1 % of the sky (0..1000), all layers

    // Cloud layers, each 0..1000 (0.1 % of the sky), at their own levels:
    int32_t cirrus;       // high ice cloud, cirrus_base..cirrus_top hPa
    int32_t altostratus;  // mid-level sheet, altostratus_base..top hPa
    int32_t cumulus;      // convective, from the LCL up to cumulus_top
    int32_t cumulonimbus; // thunderstorm cloud, from the LCL to CB_TOP
    int32_t lcl;          // lifting condensation level (cloud base), hPa
    int32_t cumulus_top;  // hPa
    int32_t cirrus_base;  // hPa: the high and middle layers drift up and down
    int32_t cirrus_top;   // and get thicker the more of them there is
    int32_t altostratus_base;
    int32_t altostratus_top;
    // Low stratiform layers, each 0..1000, all based at the LCL:
    int32_t stratus;       // shallow grey sheet in very humid air
    int32_t stratocumulus; // lumpy low layer
    int32_t nimbostratus;  // thick rain cloud of steady (non-convective)
                           // rain, from the LCL up into the middle deck
    int32_t altocumulus;   // patchy, lumpy middle cloud near altostratus
    int32_t stratus_top;   // hPa, see stratus_top()
    int32_t stratocumulus_top;
    int32_t nimbostratus_top;
    int32_t altocumulus_base; // hPa, see altocumulus_levels()
    int32_t altocumulus_top;
};

// Simulated weather. Deterministic daily patterns plus slowly varying random
// processes, so the data behaves like real weather instead of pure noise:
//
// - temperature: daily sine cycle (coldest ~03:00, warmest ~15:00) plus a
//   slow "weather front" offset that drifts over hours; clouds flatten the
//   daily swing and rain cools the air
// - cloudiness: a slow random process; when it builds past a threshold it
//   rains, harder the cloudier it gets, so rain comes in showers lasting
//   from tens of minutes to hours
// - humidity: falls when it gets warmer (relative humidity drops as air
//   warms), rises with clouds and with wetness that builds up during rain
//   and dries out over the following hours
// - wind speed: mean-reverting around a daily mean (windier in the
//   afternoon), with occasional gusts that decay
// - wind direction: a prevailing direction that drifts slowly; the vane
//   swings more in light wind than in strong wind
//
// The model steps once per simulated minute, whatever the simulation speed;
// the caller decides how fast simulated time passes (weather_station).
class weather_model
{
public:
    static constexpr uint32_t MINUTES_PER_DAY = 24 * 60;
    static constexpr uint32_t SECONDS_PER_DAY = MINUTES_PER_DAY * 60;
    static constexpr uint32_t START_MINUTE = 6 * 60; // day 1 starts at 06:00
    static constexpr uint32_t STEP_SECONDS = 60;
    static constexpr int32_t FINE = 1000; // internal resolution of the state

    // Cloudiness (0..1000) above which it rains.
    static constexpr int32_t RAIN_THRESHOLD = 650;

    // Cloud levels (hPa). Cirrus drifts around 270 hPa (~10 km) and
    // altostratus around 580 hPa (~4.5 km), within these limits; cumulus and
    // cumulonimbus grow from the LCL.
    static constexpr int32_t CIRRUS_LOWEST = 420;      // ~7 km
    static constexpr int32_t CIRRUS_HIGHEST = 190;     // ~12 km
    static constexpr int32_t ALTOSTRATUS_LOWEST = 800; // ~2 km
    static constexpr int32_t ALTOSTRATUS_HIGHEST = 420;
    static constexpr int32_t CB_TOP = 200; // the tropopause
    static constexpr int32_t SURFACE_PRESSURE = 1013;

    // Advance simulated time; the random processes step once per whole
    // simulated minute.
    void advance_simulated(uint32_t sim_seconds);

    // Jump to a time of day (0..1439) on the current day. Only the daily
    // patterns change; the slow random processes keep their state.
    void set_time_of_day(uint32_t minute);

    uint32_t day() const; // 1-based
    uint32_t second_of_day() const;
    uint32_t minute_of_day() const;

    uint64_t simulated_seconds() const
    {
        return _sim_seconds;
    }

    const weather_conditions& conditions() const
    {
        return _conditions;
    }

    // Rain that has fallen since the last call, in 0.01 mm.
    int32_t take_rain();

    // Deterministic daily patterns (exposed for tests and documentation).
    static int32_t daily_temperature(uint32_t minute_of_day);
    static int32_t daily_mean_wind(uint32_t minute_of_day);
    // Daytime convection, 0 (night) .. 1000 (mid-afternoon).
    static int32_t daily_convection(uint32_t minute_of_day);

    // Lifting condensation level from relative humidity (0.01 %RH): about
    // 125 m per degree of dew-point spread, the spread being ~(100 - RH) / 5.
    static int32_t lcl_hpa(int32_t humidity);

    // Tops of the low stratiform layers from their base (the LCL, hPa) and
    // amount (0..1000): thicker the more cloud. Functions of stored values,
    // so the log needn't keep the tops. Nimbostratus reaches the middle
    // deck: its top is the altostratus top.
    static int32_t stratus_top(int32_t lcl, int32_t amount);
    static int32_t stratocumulus_top(int32_t lcl, int32_t amount);

    // Altocumulus sits a little above the middle of the altostratus layer
    // (same drifting level), thicker with more cloud. Derived from the
    // altostratus base/top, so the log needn't keep its levels either.
    struct levels
    {
        int32_t base;
        int32_t top;
    };

    static levels altocumulus_levels(
        int32_t altostratus_base,
        int32_t altostratus_top,
        int32_t amount);

private:
    void step(uint32_t dt_seconds);
    void update_conditions();

    uint64_t _sim_seconds = START_MINUTE * 60;
    uint32_t _unstepped_seconds = 0; // less than a step since the last one
    util::xorshift32 _rng{
        0x0bad'f00du}; // seed chosen for a varied first few days

    // Random processes, in public units times FINE.
    int32_t _front = 0;                   // temperature offset, 0.01 degC
    int32_t _moisture = 0;                // humidity offset, 0.01 %RH
    int32_t _cloud_trend = 350 * FINE;    // where cloudiness is heading
    int32_t _cloud = 350 * FINE;          // cloudiness, per mille
    int32_t _wetness = 0;                 // per mille: builds in rain
    int32_t _sustained_wind = 300 * FINE; // 0.01 m/s, mean-reverting
    int32_t _gust = 0;                    // 0.01 m/s, decays every step
    int32_t _prevailing = 2250 * FINE;    // 0.1 degrees (starts SW)
    int32_t _vane_swing = 0;              // degrees (not FINE)
    // High cloud wanders independently. It has its own generator so adding
    // it didn't change the rest of the weather's random sequence.
    util::xorshift32 _cirrus_rng{0xc1'7505u};
    int32_t _cirrus = 250 * FINE; // per mille
    // Centre levels of the high and middle layers (hPa * FINE). They use
    // their own generator too, so the cirrus amount sequence is unchanged.
    util::xorshift32 _level_rng{0x1e'7e15u};
    int32_t _cirrus_level = 270 * FINE;
    int32_t _altostratus_level = 580 * FINE;
    int64_t _rain_fallen = 0; // since take_rain(), in 0.01 mm * 1 s / 1 h

    weather_conditions _conditions{};
};

} // namespace app
