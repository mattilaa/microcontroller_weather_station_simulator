# Microcontroller example

A bare-metal AArch64 program (no operating system) that runs a microcontroller-style
main loop on QEMU's `virt` machine (Cortex-A53). It is written in freestanding C++20
and built with CMake.

## Embedded C++ subset

| Flag / rule                         | Why                                                         |
|-------------------------------------|-------------------------------------------------------------|
| `-fno-exceptions`                   | no unwinder, no hidden code paths or tables                 |
| `-fno-rtti`                         | no `typeid`/`dynamic_cast`, no typeinfo data                |
| `-fno-threadsafe-statics`           | no lock around function-local statics                       |
| `-fno-use-cxa-atexit`               | no destructor registry; global objects are never destroyed  |
| `-ffreestanding -nostdinc++ -nostdlib` | no hosted libc/libc++; only compiler headers like `<stdint.h>` |
| no `operator new`/`delete` defined  | any heap use is a **link error**                            |
| all globals are `constinit`         | built at compile time, so no constructors run before `main` |
| virtual functions are fine          | `Task::run()` dispatches through a vtable; no RTTI needed   |

`src/runtime/cxx_runtime.cpp` holds the little runtime support that's left:
`__cxa_pure_virtual` (panics) and a loop that calls `.init_array` constructors. The
startup code still runs that loop, in case you add a global that isn't `constinit`.

The main loop is a small cooperative scheduler:

| Task        | Period  | What it does                                 |
|-------------|---------|----------------------------------------------|
| `heartbeat` | 500 ms  | Toggles a virtual LED                        |
| `sensor`    | 1000 ms | Makes up a temperature reading with some noise |
| `console`   | 10 ms   | Checks the UART for single-key commands      |

All tasks live in a fixed table; the `Scheduler` is given that table at compile time.

Console commands: `h` help, `s` status, `v` toggle verbose output, `q` quit QEMU.

## Requirements

- CMake ≥ 3.20 and Ninja (or Make)
- `qemu-system-aarch64`
- A cross compiler, either:
  - LLVM clang++ + lld (default): `brew install llvm lld`
  - or the Arm GNU toolchain `aarch64-none-elf-g++`

## Build & run

```sh
cmake -B build -G Ninja
cmake --build build                 # -> build/firmware.elf, build/firmware.bin, build/firmware.map
cmake --build build --target run    # run in QEMU (press 'q' to quit, or Ctrl-A X)
```

To use GCC instead of clang:

```sh
cmake -B build-gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake
```

### Debugging

```sh
cmake --build build --target debug  # QEMU halts and waits for a debugger on :1234
lldb build/firmware.elf -o "gdb-remote 1234"
```

## Layout

```
CMakeLists.txt                      build flags, objcopy to .bin, run/debug targets
cmake/toolchain-clang-aarch64.cmake clang++ + lld toolchain (default)
cmake/toolchain-gcc-aarch64.cmake   aarch64-none-elf-g++ toolchain
linker/qemu_virt.ld                 memory map: RAM at 0x40000000, .init_array, stack, .bss
src/startup.S                       _start: park secondary cores, set up the stack, enable FP, zero .bss, run constructors, call main
src/main.cpp                        static object graph + main loop
src/app/scheduler.*                 Task base class (virtual run) + cooperative Scheduler
src/app/tasks.*                     Led, TemperatureSensor, Heartbeat/Sensor/Console tasks
src/app/console.*                   type-safe `console << ...` output (replaces printf)
src/hal/mmio.hpp                    Reg32 memory-mapped register wrapper
src/hal/pl011.*                     PL011 UART driver (0x09000000)
src/hal/generic_timer.*             ARM generic timer (CNTPCT_EL0)
src/hal/semihosting.*               semihosting SYS_EXIT so QEMU can quit
src/runtime/cxx_runtime.cpp         __cxa_pure_virtual, global-constructor loop
```
