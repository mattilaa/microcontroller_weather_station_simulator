#include "app/tasks.hpp"

#include "hal/semihosting.hpp"
#include "runtime/stack_usage.hpp"

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
            "  d  dump the last 8 records\n"
            "  n  network status\n"
            "  +  faster simulation, -  slower (1x = real time)\n"
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
    _out << "--- status @ " << now_ms << " ms ---\n";
    if(_station.has_reading())
        _out << "weather: " << _station.latest() << '\n';
    _out << "stack: peak " << runtime::stack_peak() << " of "
         << runtime::stack_size() << " bytes\n"
         << "speed: " << _station.speed() << "x\n"
         << "log: " << _station.log().size() << " records, next sequence "
         << _station.log().next_sequence() << '\n';
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
    const weather_reading mean = _station.interval_mean();
    _out << "--- weather, day " << r.day << ' ' << r.time << ", speed "
         << _station.speed() << "x ---\n"
         << "temperature:    " << r.temperature << " (10-min mean "
         << mean.temperature << ")\n"
         << "humidity:       " << r.humidity << " (10-min mean "
         << mean.humidity << ")\n"
         << "wind speed:     " << r.wind_speed << " (10-min mean "
         << mean.wind_speed << ")\n"
         << "wind direction: " << r.wind_direction << " (10-min mean "
         << mean.wind_direction << ")\n"
         << "rain:           " << mean.rain << " in this interval (today "
         << r.rain_today << ")\n"
         << "cloud cover:    " << r.cloud_cover << " (10-min mean "
         << mean.cloud_cover << ")\n"
         << "cloud layers:   cirrus " << r.clouds.cirrus << " ("
         << r.clouds.cirrus_base << ".." << r.clouds.cirrus_top
         << "), altostratus " << r.clouds.altostratus << " ("
         << r.clouds.altostratus_base << ".." << r.clouds.altostratus_top
         << ")\n"
         << "                cumulus " << r.clouds.cumulus << " (top "
         << r.clouds.cumulus_top << "), cumulonimbus " << r.clouds.cumulonimbus
         << '\n'
         << "cloud base:     LCL " << r.clouds.lcl << '\n';
}

void console_task::print_history()
{
    constexpr size_t LINES = 8;
    const weather_log& log = _station.log();
    const size_t first = log.size() > LINES ? log.size() - LINES : 0;
    _out << "last " << log.size() - first
         << " 10-minute records (oldest first):\n";
    for(size_t i = first; i < log.size(); ++i)
        _out << "  " << log[i].reading() << '\n';
}

void console_task::change_speed(bool faster)
{
    static constexpr uint32_t SPEEDS[] = {1, 10, 60, 600, 3600, 72'000};
    constexpr size_t COUNT = sizeof(SPEEDS) / sizeof(SPEEDS[0]);
    size_t i = 0;
    while(i + 1 < COUNT && SPEEDS[i] < _station.speed())
        ++i;
    // i is now the first preset >= the current speed.
    if(faster && SPEEDS[i] == _station.speed() && i + 1 < COUNT)
        ++i;
    else if(!faster && i > 0)
        --i;
    _station.set_speed(SPEEDS[i]);
    _out << "speed " << _station.speed() << "x\n";
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
        case '+':
        case '-':
            change_speed(c == '+');
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
