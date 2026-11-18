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

extern Scheduler
    scheduler; // defined below; ConsoleTask only stores a reference

constinit hal::GenericTimer timer;
constinit Console console{hal::uart0};
constinit Settings settings;
constinit Led led;
constinit TemperatureSensor sensor;

constinit HeartbeatTask heartbeat_task{led, console, settings};
constinit SensorTask sensor_task{sensor, console, settings};
constinit ConsoleTask
    console_task{hal::uart0, console, settings, led, sensor, scheduler};

constinit Task* const task_table[] =
    {&heartbeat_task, &sensor_task, &console_task};
constinit Scheduler scheduler{task_table};

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

    console << "\n=== bare-metal AArch64 C++ on QEMU virt ===\n"
            << "running at EL" << current_el() << ", no operating system\n";
    console_task.print_help();

    for(;;)
    {
        scheduler.poll(timer.millis());
        // A real MCU would sleep here (wfi) until the next interrupt.
    }
}
