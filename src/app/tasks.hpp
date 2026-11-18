#pragma once

#include <stdint.h>

#include "app/console.hpp"
#include "app/scheduler.hpp"

namespace app
{

struct Settings
{
    bool verbose = true;
};

// A virtual LED (stands in for a GPIO pin).
class Led
{
public:
    void toggle()
    {
        on_ = !on_;
    }

    bool is_on() const
    {
        return on_;
    }

private:
    bool on_ = false;
};

// Temperature in hundredths of a degree, printed as "25.07 C".
struct CentiCelsius
{
    int32_t value;
};

Console& operator<<(Console& out, CentiCelsius t);

// Simulated temperature sensor: random walk with xorshift32 noise.
class TemperatureSensor
{
public:
    void sample();

    CentiCelsius value() const
    {
        return {centi_c_};
    }

private:
    uint32_t next_random();

    int32_t centi_c_ = 2500;
    uint32_t rng_state_ = 0x1234'5678u;
};

class HeartbeatTask final : public Task
{
public:
    constexpr HeartbeatTask(Led& led, Console& out, const Settings& settings)
        : Task("heartbeat", 500), led_(led), out_(out), settings_(settings)
    {
    }

    void run(uint64_t now_ms) override;

private:
    Led& led_;
    Console& out_;
    const Settings& settings_;
};

class SensorTask final : public Task
{
public:
    constexpr SensorTask(
        TemperatureSensor& sensor,
        Console& out,
        const Settings& settings)
        : Task("sensor", 1000), sensor_(sensor), out_(out), settings_(settings)
    {
    }

    void run(uint64_t now_ms) override;

private:
    TemperatureSensor& sensor_;
    Console& out_;
    const Settings& settings_;
};

// Handles single-key commands from the UART.
class ConsoleTask final : public Task
{
public:
    constexpr ConsoleTask(
        const hal::Pl011& uart,
        Console& out,
        Settings& settings,
        const Led& led,
        const TemperatureSensor& sensor,
        const Scheduler& scheduler)
        : Task("console", 10), uart_(uart), out_(out), settings_(settings),
          led_(led), sensor_(sensor), scheduler_(scheduler)
    {
    }

    void run(uint64_t now_ms) override;
    void print_help();

private:
    void print_status(uint64_t now_ms);

    const hal::Pl011& uart_;
    Console& out_;
    Settings& settings_;
    const Led& led_;
    const TemperatureSensor& sensor_;
    const Scheduler& scheduler_;
};

} // namespace app
