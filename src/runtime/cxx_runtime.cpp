// Minimal C++ runtime support for a freestanding, no-OS environment.
//
// Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics
// -fno-use-cxa-atexit, so no unwinder, typeinfo, guard locks or atexit
// registry are needed.
//
// operator new / delete are intentionally NOT defined: any accidental heap
// use (new, std::function, ...) fails at link time instead of at runtime.

#include "app/console.hpp"
#include "hal/pl011.hpp"
#include "hal/semihosting.hpp"

namespace
{
[[noreturn]] void panic(const char* msg)
{
    app::Console out{hal::uart0};
    out << "\n*** PANIC: " << msg << " ***\n";
    hal::semihosting::exit(1);
}
} // namespace

// Called if a pure virtual function is ever invoked (e.g. during construction).
extern "C" [[noreturn]] void __cxa_pure_virtual()
{
    panic("pure virtual function called");
}

// Run global constructors (.init_array). Called from startup.S before main().
extern "C"
{
    using InitFn = void (*)();
    extern InitFn __init_array_start[];
    extern InitFn __init_array_end[];

    void __run_global_constructors()
    {
        for(InitFn* fn = __init_array_start; fn != __init_array_end; ++fn)
            (*fn)();
    }
}
