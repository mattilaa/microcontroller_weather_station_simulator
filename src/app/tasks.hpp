#pragma once

#include <stdint.h>

#include "app/console.hpp"
#include "app/scheduler.hpp"
#include "util/moving_average.hpp"

namespace app
{

struct settings
{
    bool verbose = true;
};

// A virtual LED (stands in for a GPIO pin).
class led
{
public:
    void toggle()
    {
        _on = !_on;
    }

    bool is_on() const
    {
        return _on;
    }

private:
    bool _on = false;
};

// Temperature in hundredths of a degree, printed as "25.07 C".
struct centi_celsius
{
    int32_t value;
};

console& operator<<(console& out, centi_celsius t);

// Simulated temperature sensor: random walk with xorshift32 noise, smoothed
// by a moving average over the last HISTORY samples.
class temperature_sensor
{
public:
    static constexpr size_t HISTORY = 8;
    using filter = util::moving_average<int32_t, HISTORY, int64_t>;

    void sample();

    centi_celsius value() const
    {
        return {_centi_c};
    }

    centi_celsius average() const
    {
        return {_filter.average()};
    }

    const util::ring_buffer<int32_t, HISTORY>& history() const
    {
        return _filter.samples();
    }

private:
    uint32_t next_random();

    int32_t _centi_c = 2500;
    uint32_t _rng_state = 0x1234'5678u;
    filter _filter;
};

class heartbeat_task final : public task
{
public:
    constexpr heartbeat_task(led& led, console& out, const settings& settings)
        : task("heartbeat", 500), _led(led), _out(out), _settings(settings)
    {
    }

    void run(uint64_t now_ms) override;

private:
    led& _led;
    console& _out;
    const settings& _settings;
};

class sensor_task final : public task
{
public:
    constexpr sensor_task(
        temperature_sensor& sensor,
        console& out,
        const settings& settings)
        : task("sensor", 1000), _sensor(sensor), _out(out), _settings(settings)
    {
    }

    void run(uint64_t now_ms) override;

private:
    temperature_sensor& _sensor;
    console& _out;
    const settings& _settings;
};

// Handles single-key commands from the UART.
class console_task final : public task
{
public:
    constexpr console_task(
        const hal::pl011& uart,
        console& out,
        settings& settings,
        const led& led,
        const temperature_sensor& sensor,
        const scheduler& scheduler)
        : task("console", 10), _uart(uart), _out(out), _settings(settings),
          _led(led), _sensor(sensor), _scheduler(scheduler)
    {
    }

    void run(uint64_t now_ms) override;
    void print_help();

private:
    void print_status(uint64_t now_ms);
    void print_history();

    const hal::pl011& _uart;
    console& _out;
    settings& _settings;
    const led& _led;
    const temperature_sensor& _sensor;
    const scheduler& _scheduler;
};

} // namespace app
