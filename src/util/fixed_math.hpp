#pragma once

#include <stdint.h>

// Integer math helpers. The firmware is built with -mgeneral-regs-only, so
// there is no floating point at all; quantities are fixed-point integers.
namespace util
{

constexpr int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Wrap v into [0, period), also for negative v.
constexpr int32_t wrap(int32_t v, int32_t period)
{
    const int32_t r = v % period;
    return r < 0 ? r + period : r;
}

// Round v to the nearest multiple of step (halves away from zero).
constexpr int32_t round_to(int32_t v, int32_t step)
{
    if(step <= 1)
        return v;
    const int32_t half = step / 2;
    return v >= 0 ? (v + half) / step * step : -((-v + half) / step * step);
}

// sin() of an angle in whole degrees, scaled by SIN_SCALE.
// Bhaskara I's approximation: max error about 0.0016 (16 in SIN_SCALE units).
inline constexpr int32_t SIN_SCALE = 10000;

constexpr int32_t sin_deg(int32_t degrees)
{
    int32_t d = wrap(degrees, 360);
    int32_t sign = 1;
    if(d >= 180)
    {
        d -= 180;
        sign = -1;
    }
    const int32_t p = d * (180 - d); // 0 .. 8100
    return sign * (4 * SIN_SCALE * p) / (40500 - p);
}

constexpr int32_t cos_deg(int32_t degrees)
{
    return sin_deg(degrees + 90);
}

// Pressure in hPa at a height in metres in the ICAO standard atmosphere
// (1013.25 hPa at sea level), by linear interpolation in a 500 m table.
// Accurate to about 1 hPa up to 12 km; clamped outside 0..12 km.
constexpr int32_t standard_pressure_hpa(int32_t metres)
{
    // Pressure in 0.1 hPa every 500 m, 0..12000 m.
    constexpr int32_t TABLE[] =
        {10133,
         9546,
         8988,
         8456,
         7950,
         7469,
         7012,
         6578,
         6164,
         5770,
         5405,
         5054,
         4722,
         4408,
         4111,
         3830,
         3565,
         3315,
         3080,
         2858,
         2650,
         2454,
         2270,
         2098,
         1940};
    constexpr int32_t STEP = 500;
    constexpr int32_t LAST =
        static_cast<int32_t>(sizeof(TABLE) / sizeof(TABLE[0])) - 1;
    if(metres <= 0)
        return (TABLE[0] + 5) / 10;
    if(metres >= LAST * STEP)
        return (TABLE[LAST] + 5) / 10;
    const int32_t i = metres / STEP;
    const int32_t within = metres - i * STEP;
    const int32_t tenths = TABLE[i] + (TABLE[i + 1] - TABLE[i]) * within / STEP;
    return (tenths + 5) / 10;
}

} // namespace util
