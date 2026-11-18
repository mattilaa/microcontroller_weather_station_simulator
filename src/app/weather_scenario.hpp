#pragma once

#include <stddef.h>
#include <stdint.h>

#include "util/span.hpp"

namespace app
{

// The climate the weather model simulates: the means and strengths of its
// daily cycles and random processes. Selected at boot with
//   -fw_cfg name=opt/weather/scenario,string=<name>
// or at run time with the UDP command "scenario <name>" or the console key
// 'c'. "default" reproduces the original temperate weather exactly.
struct weather_scenario
{
    const char* name;
    const char* description;

    // Temperature: daily sine cycle (coldest ~03:00, warmest ~15:00).
    int32_t temperature_mean;  // 0.01 degC
    int32_t temperature_swing; // 0.01 degC above/below the mean

    // Relative humidity at the mean temperature, and how much it drops per
    // degree of warming (0.01 %RH per 0.01 degC).
    int32_t humidity_base;       // 0.01 %RH
    int32_t humidity_per_degree; // 0.01 %RH per 0.01 degC

    // Cloudiness (per mille of low + middle cloud): where it drifts, how
    // restless it is, and how much extra cloud afternoon convection builds
    // on top (following the convection, peaking mid-afternoon).
    int32_t cloud_mean;
    int32_t cloud_trend_noise; // per mille per minute
    int32_t convective_cloud;  // per mille added at peak convection

    // Daytime convection (0..1000 of the daily curve): cumulus, altocumulus
    // and, with enough cloud, cumulonimbus.
    int32_t convection;
    int32_t storm_cloud;      // cloudiness above which cumulonimbus forms
    int32_t storm_convection; // convection it needs (0..1000)

    // Rain: falls above this cloudiness, rate_per_cloud * excess / 1000 in
    // 0.01 mm/h (1500: 1.5 * 0.01 mm/h per per mille of cloud).
    int32_t rain_threshold;
    int32_t rain_per_cloud;

    // Wind: daily mean and swing (windiest mid-afternoon), gusts per minute
    // (chance per 1000, size range), prevailing direction and how fast it
    // turns clockwise (veers), e.g. as a front passes.
    int32_t wind_mean;  // 0.01 m/s
    int32_t wind_swing; // 0.01 m/s
    int32_t gust_chance;
    int32_t gust_min;   // 0.01 m/s
    int32_t gust_max;   // 0.01 m/s
    int32_t prevailing; // 0.1 degrees (where the wind comes from)
    int32_t veering;    // 0.1 degrees per hour

    // Surface pressure: mean, and a dip that deepens over the first 12
    // hours and fills again over the next 12 (a passing low).
    int32_t pressure_mean; // 0.1 hPa
    int32_t pressure_dip;  // 0.1 hPa

    int32_t cirrus_mean; // per mille of high cloud
};

inline constexpr weather_scenario SCENARIOS[] = {
    {
        .name = "default",
        .description = "temperate: 7..23 C, showers now and then",
        .temperature_mean = 1500,
        .temperature_swing = 800,
        .humidity_base = 6500,
        .humidity_per_degree = 3,
        .cloud_mean = 350,
        .cloud_trend_noise = 19,
        .convective_cloud = 0,
        .convection = 1000,
        .storm_cloud = 700,
        .storm_convection = 300,
        .rain_threshold = 650,
        .rain_per_cloud = 1500,
        .wind_mean = 300,
        .wind_swing = 200,
        .gust_chance = 6,
        .gust_min = 100,
        .gust_max = 300,
        .prevailing = 2250,
        .veering = 0,
        .pressure_mean = 10132,
        .pressure_dip = 0,
        .cirrus_mean = 250,
    },
    {
        .name = "summer_heat",
        .description = "very hot summer day: 18..36 C, dry, mostly clear",
        .temperature_mean = 2700,
        .temperature_swing = 900,
        .humidity_base = 4500,
        .humidity_per_degree = 3,
        .cloud_mean = 150,
        .cloud_trend_noise = 12,
        .convective_cloud = 150,
        .convection = 1000,
        .storm_cloud = 850,
        .storm_convection = 400,
        .rain_threshold = 800,
        .rain_per_cloud = 1500,
        .wind_mean = 250,
        .wind_swing = 150,
        .gust_chance = 5,
        .gust_min = 100,
        .gust_max = 300,
        .prevailing = 1800,
        .veering = 0,
        .pressure_mean = 10195,
        .pressure_dip = 20,
        .cirrus_mean = 120,
    },
    {
        .name = "winter_high",
        .description = "cold winter high pressure: -16..-8 C, calm, clear, "
                       "frost fog at night",
        .temperature_mean = -1200,
        .temperature_swing = 400,
        .humidity_base = 8500,
        .humidity_per_degree = 2,
        .cloud_mean = 120,
        .cloud_trend_noise = 10,
        .convective_cloud = 0,
        .convection = 0,
        .storm_cloud = 1001, // never
        .storm_convection = 1001,
        .rain_threshold = 800,
        .rain_per_cloud = 500,
        .wind_mean = 150,
        .wind_swing = 50,
        .gust_chance = 2,
        .gust_min = 50,
        .gust_max = 150,
        .prevailing = 900,
        .veering = 0,
        .pressure_mean = 10380,
        .pressure_dip = 0,
        .cirrus_mean = 80,
    },
    {
        .name = "storm",
        .description = "severe storm: deep low, gales, heavy rain, wind "
                       "veering from SSW to NW",
        .temperature_mean = 900,
        .temperature_swing = 200,
        .humidity_base = 8500,
        .humidity_per_degree = 2,
        .cloud_mean = 820,
        .cloud_trend_noise = 15,
        .convective_cloud = 0,
        .convection = 200,
        .storm_cloud = 900,
        .storm_convection = 500,
        .rain_threshold = 600,
        .rain_per_cloud = 1000,
        .wind_mean = 1300,
        .wind_swing = 200,
        .gust_chance = 40,
        .gust_min = 400,
        .gust_max = 900,
        .prevailing = 2000,
        .veering = 60,
        .pressure_mean = 9920,
        .pressure_dip = 240,
        .cirrus_mean = 200,
    },
    {
        .name = "thunderstorm",
        .description = "hot humid day with afternoon thunderstorms: "
                       "cumulonimbus, cloudbursts, gusts",
        .temperature_mean = 2300,
        .temperature_swing = 700,
        .humidity_base = 7500,
        .humidity_per_degree = 2,
        .cloud_mean = 420,
        .cloud_trend_noise = 10,
        .convective_cloud = 700,
        .convection = 1000,
        .storm_cloud = 650,
        .storm_convection = 200,
        .rain_threshold = 650,
        .rain_per_cloud = 2200,
        .wind_mean = 250,
        .wind_swing = 150,
        .gust_chance = 15,
        .gust_min = 300,
        .gust_max = 1000,
        .prevailing = 2250,
        .veering = 0,
        .pressure_mean = 10080,
        .pressure_dip = 30,
        .cirrus_mean = 300,
    },
};

inline constexpr size_t SCENARIO_COUNT =
    sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

// The scenario called `name`, or nullptr.
const weather_scenario* find_scenario(util::span<const uint8_t> name);

} // namespace app
