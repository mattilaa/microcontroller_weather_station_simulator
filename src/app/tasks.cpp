#include "app/tasks.hpp"

#include "hal/semihosting.hpp"

namespace app
{

namespace
{
Console& timestamp(Console& out, uint64_t now_ms)
{
    return out << '[' << dec(now_ms, 6) << " ms] ";
}
} // namespace

Console& operator<<(Console& out, CentiCelsius t)
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

// ---- TemperatureSensor ------------------------------------------------------

uint32_t TemperatureSensor::next_random()
{
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return rng_state_;
}

void TemperatureSensor::sample()
{
    constexpr int32_t min = 1500;
    constexpr int32_t max = 3500;
    const int32_t delta =
        static_cast<int32_t>(next_random() % 51) - 25; // +/- 0.25 C
    int32_t v = centi_c_ + delta;
    centi_c_ = v < min ? min : (v > max ? max : v);
}

// ---- Tasks ------------------------------------------------------------------

void HeartbeatTask::run(uint64_t now_ms)
{
    led_.toggle();
    if(settings_.verbose)
        timestamp(out_, now_ms)
            << "LED " << (led_.is_on() ? "ON" : "OFF") << '\n';
}

void SensorTask::run(uint64_t now_ms)
{
    sensor_.sample();
    if(settings_.verbose)
        timestamp(out_, now_ms) << "temp = " << sensor_.value() << '\n';
}

void ConsoleTask::print_help()
{
    out_ << "Commands:\n"
            "  h  help\n"
            "  s  status\n"
            "  v  toggle verbose output\n"
            "  q  quit QEMU (semihosting)\n";
}

void ConsoleTask::print_status(uint64_t now_ms)
{
    out_ << "--- status @ " << now_ms << " ms ---\n"
         << "LED: " << (led_.is_on() ? "on" : "off")
         << ", temp: " << sensor_.value() << '\n';
    for(const Task* task : scheduler_)
        out_ << "  task " << task->name() << ": period " << task->period_ms()
             << " ms, runs " << task->runs() << '\n';
}

void ConsoleTask::run(uint64_t now_ms)
{
    char c;
    while(uart_.try_get(c))
    {
        switch(c)
        {
        case 'h':
            print_help();
            break;
        case 's':
            print_status(now_ms);
            break;
        case 'v':
            settings_.verbose = !settings_.verbose;
            out_ << "verbose " << (settings_.verbose ? "on" : "off") << '\n';
            break;
        case 'q':
            out_ << "bye!\n";
            hal::semihosting::exit(0);
        case '\r':
        case '\n':
            break;
        default:
            out_ << "unknown command '" << c << "' (h for help)\n";
            break;
        }
    }
}

} // namespace app
