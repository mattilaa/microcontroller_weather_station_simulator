#include "app/weather_service.hpp"

namespace app
{

namespace
{
// The request text without trailing whitespace / newlines.
util::span<const uint8_t> trim(util::span<const uint8_t> text)
{
    size_t size = text.size();
    while(size > 0 && (text[size - 1] == '\n' || text[size - 1] == '\r' ||
                       text[size - 1] == ' '))
        --size;
    return text.first(size);
}

bool equals(util::span<const uint8_t> text, const char* word)
{
    size_t i = 0;
    for(; word[i] != '\0'; ++i)
        if(i >= text.size() || text[i] != static_cast<uint8_t>(word[i]))
            return false;
    return i == text.size();
}

bool starts_with(util::span<const uint8_t> text, const char* prefix)
{
    for(size_t i = 0; prefix[i] != '\0'; ++i)
        if(i >= text.size() || text[i] != static_cast<uint8_t>(prefix[i]))
            return false;
    return true;
}

int digit(uint8_t c)
{
    return c >= '0' && c <= '9' ? c - '0' : -1;
}

// Parse "HH:MM" into minutes since midnight; -1 if invalid.
int32_t parse_clock(util::span<const uint8_t> text)
{
    if(text.size() != 5 || text[2] != ':')
        return -1;
    const int h1 = digit(text[0]);
    const int h2 = digit(text[1]);
    const int m1 = digit(text[3]);
    const int m2 = digit(text[4]);
    if(h1 < 0 || h2 < 0 || m1 < 0 || m2 < 0)
        return -1;
    const int hours = h1 * 10 + h2;
    const int minutes = m1 * 10 + m2;
    if(hours > 23 || minutes > 59)
        return -1;
    return hours * 60 + minutes;
}

void write_json(util::text_stream& out, const weather_reading& r)
{
    out << "{\"day\":" << r.day << ",\"time\":\"" << r.time << '"'
        << ",\"temperature\":" << util::fixed(r.temperature.value, 2)
        << ",\"humidity\":" << util::fixed(r.humidity.value, 2)
        << ",\"wind_speed\":" << util::fixed(r.wind_speed.value, 2)
        << ",\"wind_direction\":" << r.wind_direction.value
        << ",\"wind_compass\":\"" << compass_point(r.wind_direction.value)
        << "\"}\n";
}
} // namespace

size_t weather_service::handle(
    util::span<const uint8_t> request,
    const net::udp_endpoint&,
    util::span<uint8_t> reply)
{
    util::buffer_stream
        out{reinterpret_cast<char*>(reply.data()), reply.size()};
    const util::span<const uint8_t> command = trim(request);

    if(equals(command, "weather"))
    {
        if(_station.has_reading())
            write_json(out, _station.latest());
        else
            out << "error: no reading yet\n";
    }
    else if(starts_with(command, "time "))
    {
        const int32_t minute = parse_clock(command.subspan(5));
        if(minute < 0)
            out << "error: expected time HH:MM\n";
        else
        {
            _station.set_time_of_day(static_cast<uint32_t>(minute));
            out << "ok time " << clock_time{static_cast<uint32_t>(minute)}
                << '\n';
        }
    }
    else if(equals(command, "ping"))
        out << "pong\n";
    else if(equals(command, "help"))
        out << "commands: weather, time HH:MM, ping, help\n";
    else
        out << "error: unknown command\n";

    return out.size();
}

} // namespace app
