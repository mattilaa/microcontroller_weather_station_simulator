#pragma once

#include <stdint.h>

#include "util/text_stream.hpp"

// Fixed-point physical quantities. Each prints with its unit.
namespace app
{

struct centi_celsius // 0.01 degC, prints "12.34 C"
{
    int32_t value;
};

struct centi_percent // 0.01 % relative humidity, prints "71.20 %RH"
{
    int32_t value;
};

struct centi_mps // 0.01 m/s, prints "3.42 m/s"
{
    int32_t value;
};

struct percent // whole percent, e.g. cloud cover, prints "63 %"
{
    int32_t value;
};

struct centi_mm // 0.01 mm of precipitation, prints "0.40 mm"
{
    int32_t value;
};

// Direction the wind blows from, 0..359; prints "225 deg SW".
struct compass_degrees
{
    int32_t value;
};

struct clock_time // simulated time of day, prints "06:10:05"
{
    uint32_t second_of_day;
};

struct clock_minute // simulated time of day to the minute, prints "06:10"
{
    uint32_t minute_of_day;
};

// 16-point compass name ("N", "NNE", ... "NNW") for a direction in degrees.
const char* compass_point(int32_t degrees);

util::text_stream& operator<<(util::text_stream& out, centi_celsius t);
util::text_stream& operator<<(util::text_stream& out, centi_percent h);
util::text_stream& operator<<(util::text_stream& out, centi_mps s);
util::text_stream& operator<<(util::text_stream& out, centi_mm r);
util::text_stream& operator<<(util::text_stream& out, percent p);
util::text_stream& operator<<(util::text_stream& out, compass_degrees d);
util::text_stream& operator<<(util::text_stream& out, clock_time t);
util::text_stream& operator<<(util::text_stream& out, clock_minute t);

} // namespace app
