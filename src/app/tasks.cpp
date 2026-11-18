#include "app/tasks.hpp"

#include "hal/semihosting.hpp"

namespace app
{

namespace
{
util::text_stream& timestamp(util::text_stream& out, uint64_t now_ms)
{
    return out << '[' << util::dec(now_ms, 6) << " ms] ";
}
} // namespace

// ---- Tasks ------------------------------------------------------------------

void heartbeat_task::run(uint64_t now_ms)
{
    _led.toggle();
    if(_settings.verbose)
        timestamp(_out, now_ms)
            << "LED " << (_led.is_on() ? "ON" : "OFF") << '\n';
}

void weather_task::run(uint64_t now_ms)
{
    _station.update(now_ms);
    if(_settings.verbose)
        timestamp(_out, now_ms) << _station.latest() << '\n';
}

void network_task::run(uint64_t)
{
    if(_nic.ready())
        _stack.poll();
}

// ---- Console ----------------------------------------------------------------

void console_task::print_help()
{
    _out << "Commands:\n"
            "  h  help\n"
            "  s  status\n"
            "  w  weather report (latest and averages)\n"
            "  d  dump weather history\n"
            "  n  network status\n"
            "  v  toggle verbose output\n"
            "  q  quit QEMU (semihosting)\n";
}

void console_task::print_network()
{
    if(!_nic.ready())
    {
        _out << "network: " << hal::to_string(_nic.state()) << '\n';
        return;
    }
    const net::stack::counters& stats = _stack.stats();
    _out << "network: up, MAC " << _nic.mac() << ", IP "
         << _stack.config().address << ", UDP port " << _stack.udp_port()
         << '\n'
         << "  frames rx " << stats.frames_received << ", tx "
         << stats.frames_sent << ", dropped " << stats.dropped << '\n'
         << "  arp replies " << stats.arp_replies << ", echo replies "
         << stats.echo_replies << ", udp requests " << stats.udp_requests
         << '\n';
}

void console_task::print_status(uint64_t now_ms)
{
    _out << "--- status @ " << now_ms << " ms ---\n"
         << "LED: " << (_led.is_on() ? "on" : "off") << '\n';
    if(_station.has_reading())
        _out << "weather: " << _station.latest() << '\n';
    print_network();
    for(const task* t : _scheduler)
        _out << "  task " << t->name() << ": period " << t->period_ms()
             << " ms, runs " << t->runs() << '\n';
}

void console_task::print_weather()
{
    if(!_station.has_reading())
    {
        _out << "no reading yet\n";
        return;
    }
    const weather_reading& r = _station.latest();
    const weather_reading avg = _station.average();
    _out << "--- weather, day " << r.day << ' ' << r.time << " ---\n"
         << "temperature:    " << r.temperature << " (avg " << avg.temperature
         << ")\n"
         << "humidity:       " << r.humidity << " (avg " << avg.humidity
         << ")\n"
         << "wind speed:     " << r.wind_speed << " (avg " << avg.wind_speed
         << ")\n"
         << "wind direction: " << r.wind_direction << " (avg "
         << avg.wind_direction << ")\n";
}

void console_task::print_history()
{
    const auto& history = _station.history();
    _out << "last " << history.size() << " of " << history.capacity()
         << " readings (oldest first):\n";
    for(size_t i = 0; i < history.size(); ++i)
        _out << "  " << history[i] << '\n';
}

void console_task::run(uint64_t now_ms)
{
    char c;
    while(_uart.try_get(c))
    {
        switch(c)
        {
        case 'h':
            print_help();
            break;
        case 's':
            print_status(now_ms);
            break;
        case 'w':
            print_weather();
            break;
        case 'd':
            print_history();
            break;
        case 'n':
            print_network();
            break;
        case 'v':
            _settings.verbose = !_settings.verbose;
            _out << "verbose " << (_settings.verbose ? "on" : "off") << '\n';
            break;
        case 'q':
            _out << "bye!\n";
            hal::semihosting::exit(0);
        case '\r':
        case '\n':
            break;
        default:
            _out << "unknown command '" << c << "' (h for help)\n";
            break;
        }
    }
}

} // namespace app
