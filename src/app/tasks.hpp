#pragma once

#include <stdint.h>

#include "app/scheduler.hpp"
#include "app/weather_station.hpp"
#include "hal/pl011.hpp"
#include "hal/virtio_net.hpp"
#include "net/stack.hpp"
#include "util/text_stream.hpp"

namespace app
{

struct settings
{
    bool verbose = true;
};

// Reads all weather sensors once per second.
class weather_task final : public task
{
public:
    constexpr weather_task(
        weather_station& station,
        util::text_stream& out,
        const settings& settings)
        : task("weather", 1000), _station(station), _out(out),
          _settings(settings)
    {
    }

    void run(uint64_t now_ms) override;

private:
    weather_station& _station;
    util::text_stream& _out;
    const settings& _settings;
};

// Polls the network interface and handles received frames.
class network_task final : public task
{
public:
    constexpr network_task(const hal::virtio_net& nic, net::stack& stack)
        : task("network", 5), _nic(nic), _stack(stack)
    {
    }

    void run(uint64_t now_ms) override;

private:
    const hal::virtio_net& _nic;
    net::stack& _stack;
};

// Handles single-key commands from the UART.
class console_task final : public task
{
public:
    constexpr console_task(
        const hal::pl011& uart,
        util::text_stream& out,
        settings& settings,
        weather_station& station,
        const hal::virtio_net& nic,
        const net::stack& stack,
        const scheduler& scheduler)
        : task("console", 10), _uart(uart), _out(out), _settings(settings),
          _station(station), _nic(nic), _stack(stack), _scheduler(scheduler)
    {
    }

    void run(uint64_t now_ms) override;
    void print_help();
    void print_network();

private:
    void print_status(uint64_t now_ms);
    void print_weather();
    void print_history();
    void change_speed(bool faster);
    void next_scenario();

    const hal::pl011& _uart;
    util::text_stream& _out;
    settings& _settings;
    weather_station& _station;
    const hal::virtio_net& _nic;
    const net::stack& _stack;
    const scheduler& _scheduler;
};

} // namespace app
