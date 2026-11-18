#include "app/tasks.hpp"

#include "hal/semihosting.hpp"

namespace app
{

namespace
{
console& timestamp(console& out, uint64_t now_ms)
{
    return out << '[' << dec(now_ms, 6) << " ms] ";
}
} // namespace

console& operator<<(console& out, centi_celsius t)
{
    int32_t v = t.value;
    if(v < 0)
    {
        out << '-';
        v = -v;
    }
    return out << v / 100 << '.' << dec(static_cast<uint64_t>(v % 100), 2, '0')
               << " C";
}

// ---- temperature_sensor
// ------------------------------------------------------

uint32_t temperature_sensor::next_random()
{
    _rng_state ^= _rng_state << 13;
    _rng_state ^= _rng_state >> 17;
    _rng_state ^= _rng_state << 5;
    return _rng_state;
}

void temperature_sensor::sample()
{
    constexpr int32_t MIN_CENTI_C = 1500;
    constexpr int32_t MAX_CENTI_C = 3500;
    const int32_t delta =
        static_cast<int32_t>(next_random() % 51) - 25; // +/- 0.25 C
    int32_t v = _centi_c + delta;
    _centi_c =
        v < MIN_CENTI_C ? MIN_CENTI_C : (v > MAX_CENTI_C ? MAX_CENTI_C : v);
    _filter.add(_centi_c);
}

// ---- Tasks ------------------------------------------------------------------

void heartbeat_task::run(uint64_t now_ms)
{
    _led.toggle();
    if(_settings.verbose)
        timestamp(_out, now_ms)
            << "LED " << (_led.is_on() ? "ON" : "OFF") << '\n';
}

void sensor_task::run(uint64_t now_ms)
{
    _sensor.sample();
    if(_settings.verbose)
        timestamp(_out, now_ms) << "temp = " << _sensor.value() << " (avg "
                                << _sensor.average() << ")\n";
}

void console_task::print_help()
{
    _out << "Commands:\n"
            "  h  help\n"
            "  s  status\n"
            "  d  dump temperature history\n"
            "  v  toggle verbose output\n"
            "  q  quit QEMU (semihosting)\n";
}

void console_task::print_status(uint64_t now_ms)
{
    _out << "--- status @ " << now_ms << " ms ---\n"
         << "LED: " << (_led.is_on() ? "on" : "off")
         << ", temp: " << _sensor.value() << ", avg: " << _sensor.average()
         << '\n';
    for(const task* t : _scheduler)
        _out << "  task " << t->name() << ": period " << t->period_ms()
             << " ms, runs " << t->runs() << '\n';
}

void console_task::print_history()
{
    const auto& history = _sensor.history();
    _out << "last " << history.size() << " of " << history.capacity()
         << " samples (oldest first):\n";
    for(size_t i = 0; i < history.size(); ++i)
        _out << "  " << centi_celsius{history[i]} << '\n';
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
        case 'd':
            print_history();
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
