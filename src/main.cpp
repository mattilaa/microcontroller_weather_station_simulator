// Bare-metal "microcontroller-style" main loop on QEMU virt (AArch64), in C++.
//
// Every object is statically allocated and constant-initialized (constinit):
// no heap, no exceptions, no RTTI, and no constructors run before main().

#include <stdint.h>

#include "app/console.hpp"
#include "app/scheduler.hpp"
#include "app/tasks.hpp"
#include "app/weather_service.hpp"
#include "app/weather_station.hpp"
#include "hal/generic_timer.hpp"
#include "hal/pl011.hpp"
#include "hal/virtio_net.hpp"
#include "net/stack.hpp"

namespace
{

using namespace app;

// Static addressing that matches QEMU's user-mode network (slirp) defaults.
constexpr net::ipv4_config NETWORK_CONFIG{
    .address = {{10, 0, 2, 15}},
    .netmask = {{255, 255, 255, 0}},
    .gateway = {{10, 0, 2, 2}},
};

extern scheduler
    task_scheduler; // defined below; console_task keeps a reference

constinit hal::generic_timer timer;
constinit console serial_console{hal::uart0};
constinit settings app_settings;
constinit weather_log reading_log{}; // large and all zero: lands in .bss
constinit weather_station station{reading_log};

constinit hal::virtio_net::dma_memory ethernet_memory{}; // .bss
constinit hal::virtio_net ethernet{ethernet_memory};
constinit weather_service weather_udp{station};
constinit net::stack
    ip_stack{ethernet, NETWORK_CONFIG, weather_udp, weather_service::PORT};

constinit weather_task weather_reader{station, serial_console, app_settings};
constinit network_task network{ethernet, ip_stack};
constinit console_task command_shell{
    hal::uart0,
    serial_console,
    app_settings,
    station,
    ethernet,
    ip_stack,
    task_scheduler};

constinit task* const task_table[] =
    {&weather_reader, &network, &command_shell};
constinit scheduler task_scheduler{task_table};

unsigned current_el()
{
    uint64_t el;
    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    return static_cast<unsigned>((el >> 2) & 3);
}

} // namespace

// C linkage: in freestanding mode main() is an ordinary (mangled) function,
// and startup.S calls it by its plain symbol name.
extern "C" [[noreturn]] void main()
{
    hal::uart0.init();
    timer.init();

    serial_console << "\n=== bare-metal AArch64 C++ on QEMU virt ===\n"
                   << "running at EL" << current_el()
                   << ", no operating system\n";

    if(ethernet.init() == hal::virtio_net::status::ready)
        ip_stack.announce();
    command_shell.print_network();
    command_shell.print_help();

    for(;;)
    {
        task_scheduler.poll(timer.millis());
        // A real MCU would sleep here (wfi) until the next interrupt.
    }
}
