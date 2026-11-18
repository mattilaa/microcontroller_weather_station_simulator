// Bare-metal "microcontroller-style" main loop on QEMU virt (AArch64), in C++.
//
// Every object is statically allocated and constant-initialized (constinit):
// no heap, no exceptions, no RTTI, and no constructors run before main().

#include <stdint.h>

#include "app/console.hpp"
#include "app/scheduler.hpp"
#include "app/tasks.hpp"
#include "hal/generic_timer.hpp"
#include "hal/pl011.hpp"

namespace
{

using namespace app;

extern scheduler
    task_scheduler; // defined below; console_task keeps a reference

constinit hal::generic_timer timer;
constinit console serial_console{hal::uart0};
constinit settings app_settings;
constinit led status_led;
constinit temperature_sensor temp_sensor;

constinit heartbeat_task heartbeat{status_led, serial_console, app_settings};
constinit sensor_task sensor_reader{temp_sensor, serial_console, app_settings};
constinit console_task command_shell{
    hal::uart0,
    serial_console,
    app_settings,
    status_led,
    temp_sensor,
    task_scheduler};

constinit task* const task_table[] =
    {&heartbeat, &sensor_reader, &command_shell};
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
    command_shell.print_help();

    for(;;)
    {
        task_scheduler.poll(timer.millis());
        // A real MCU would sleep here (wfi) until the next interrupt.
    }
}
