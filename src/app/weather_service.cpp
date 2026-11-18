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

// Parse a decimal number of at most 9 digits; -1 if invalid.
int32_t parse_number(util::span<const uint8_t> text)
{
    if(text.empty() || text.size() > 9)
        return -1;
    int32_t value = 0;
    for(uint8_t c : text)
    {
        if(digit(c) < 0)
            return -1;
        value = value * 10 + digit(c);
    }
    return value;
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
        << "\",\"rain\":" << util::fixed(r.rain.value, 2)
        << ",\"rain_today\":" << util::fixed(r.rain_today.value, 2);
}

// One log row: seq,day,time,temperature,humidity,wind_speed,wind_direction,rain
void write_csv_row(util::text_stream& out, const weather_log::entry& e)
{
    const weather_reading& r = e.reading;
    out << e.sequence << ',' << r.day << ','
        << clock_minute{r.time.second_of_day / 60} << ','
        << util::fixed(r.temperature.value, 2) << ','
        << util::fixed(r.humidity.value, 2) << ','
        << util::fixed(r.wind_speed.value, 2) << ',' << r.wind_direction.value
        << ',' << util::fixed(r.rain.value, 2) << '\n';
}

// A page of the log, starting at sequence number `from`:
//   log next=<sequence to ask for next> count=<rows in this reply>
//   <rows>
// The client repeats with "log <next>" until count=0.
void write_log_page(
    util::text_stream& out,
    const weather_log& log,
    uint32_t from)
{
    constexpr size_t ROWS_PER_PAGE = 20; // 20 rows of < 64 bytes fit a frame
    const size_t first = log.find(from);
    const size_t available = log.size() - first;
    const size_t count = available < ROWS_PER_PAGE ? available : ROWS_PER_PAGE;
    const uint32_t next =
        count > 0 ? log[first + count - 1].sequence + 1 : log.next_sequence();
    out << "log next=" << next << " count=" << count << '\n';
    for(size_t i = first; i < first + count; ++i)
        write_csv_row(out, log[i]);
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
        {
            write_json(out, _station.latest());
            out << ",\"speed\":" << _station.speed() << "}\n";
        }
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
            out << "ok time " << clock_minute{static_cast<uint32_t>(minute)}
                << '\n';
        }
    }
    else if(equals(command, "log"))
        write_log_page(out, _station.log(), 0);
    else if(starts_with(command, "log "))
    {
        const int32_t from = parse_number(command.subspan(4));
        if(from < 0)
            out << "error: expected log <sequence>\n";
        else
            write_log_page(out, _station.log(), static_cast<uint32_t>(from));
    }
    else if(starts_with(command, "advance "))
    {
        constexpr int32_t MAX_MINUTES = 3 * 24 * 60;
        const int32_t minutes = parse_number(command.subspan(8));
        if(minutes < 10 || minutes > MAX_MINUTES)
            out << "error: expected advance <minutes> (10.." << MAX_MINUTES
                << ")\n";
        else
        {
            _station.fast_forward(static_cast<uint32_t>(minutes));
            const weather_reading& r = _station.latest();
            out << "ok advanced to day " << r.day << ' '
                << clock_minute{r.time.second_of_day / 60} << '\n';
        }
    }
    else if(equals(command, "speed"))
        out << "speed " << _station.speed() << '\n';
    else if(starts_with(command, "speed "))
    {
        const int32_t speed = parse_number(command.subspan(6));
        if(speed < 1 ||
           speed > static_cast<int32_t>(weather_station::MAX_SPEED))
            out << "error: expected speed <1.." << weather_station::MAX_SPEED
                << ">\n";
        else
        {
            _station.set_speed(static_cast<uint32_t>(speed));
            out << "ok speed " << _station.speed() << '\n';
        }
    }
    else if(equals(command, "ping"))
        out << "pong\n";
    else if(equals(command, "help"))
        out << "commands: weather, time HH:MM, log [sequence], "
               "advance <minutes>, speed [N], ping, help\n";
    else
        out << "error: unknown command\n";

    return out.size();
}

} // namespace app
