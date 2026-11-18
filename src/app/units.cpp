#include "app/units.hpp"

#include "util/fixed_math.hpp"

namespace app
{

const char* compass_point(int32_t degrees)
{
    static constexpr const char* POINTS[16] =
        {"N",
         "NNE",
         "NE",
         "ENE",
         "E",
         "ESE",
         "SE",
         "SSE",
         "S",
         "SSW",
         "SW",
         "WSW",
         "W",
         "WNW",
         "NW",
         "NNW"};
    // Each point covers 22.5 degrees, centred on its nominal direction.
    const int32_t tenths = util::wrap(degrees, 360) * 10;
    return POINTS[((tenths + 112) / 225) % 16];
}

util::text_stream& operator<<(util::text_stream& out, centi_celsius t)
{
    return out << util::fixed(t.value, 2) << " C";
}

util::text_stream& operator<<(util::text_stream& out, centi_percent h)
{
    return out << util::fixed(h.value, 2) << " %RH";
}

util::text_stream& operator<<(util::text_stream& out, centi_mps s)
{
    return out << util::fixed(s.value, 2) << " m/s";
}

util::text_stream& operator<<(util::text_stream& out, centi_mm r)
{
    return out << util::fixed(r.value, 2) << " mm";
}

util::text_stream& operator<<(util::text_stream& out, percent p)
{
    return out << p.value << " %";
}

util::text_stream& operator<<(util::text_stream& out, hectopascal p)
{
    return out << p.value << " hPa";
}

util::text_stream& operator<<(util::text_stream& out, compass_degrees d)
{
    return out << d.value << " deg " << compass_point(d.value);
}

util::text_stream& operator<<(util::text_stream& out, clock_time t)
{
    return out << clock_minute{t.second_of_day / 60} << ':'
               << util::dec(t.second_of_day % 60, 2, '0');
}

util::text_stream& operator<<(util::text_stream& out, clock_minute t)
{
    return out << util::dec(t.minute_of_day / 60, 2, '0') << ':'
               << util::dec(t.minute_of_day % 60, 2, '0');
}

} // namespace app
