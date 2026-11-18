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
| virtual functions are fine          | `task::run()` dispatches through a vtable; no RTTI needed   |

Templates are encouraged: they are resolved entirely at compile time and need no
runtime support. Just watch code size, because every distinct `<T, N>` combination
generates its own copy of the code (check `firmware.map`).

| Template                          | Where              | Shows                                             |
|-----------------------------------|--------------------|---------------------------------------------------|
| `util::ring_buffer<T, N>`          | `src/util/`        | fixed-capacity container, `static_assert` on `N`  |
| `util::moving_average<T, N, Acc>`  | `src/util/`        | C++20 `concept` constraint, O(1) running sum      |
| `hal::bit_field<Shift, Width>`     | `src/hal/mmio.hpp` | register fields checked at compile time (used by the PL011 driver) |
| `scheduler(task* const (&)[N])`   | `src/app/`         | array size deduced from the task table            |

`src/util/static_tests.cpp` holds compile-time unit tests (`static_assert` on
`constexpr` code). A failing test breaks the build, and the file adds 0 bytes to the image.

`src/runtime/cxx_runtime.cpp` holds the little runtime support that's left:
`__cxa_pure_virtual` (panics) and a loop that calls `.init_array` constructors. The
startup code still runs that loop, in case you add a global that isn't `constinit`.

The main loop is a small cooperative scheduler:

| Task        | Period  | What it does                                 |
|-------------|---------|----------------------------------------------|
| `heartbeat` | 500 ms  | Toggles a virtual LED                        |
| `sensor`    | 1000 ms | Simulated temperature + moving average of the last 8 samples |
| `console`   | 10 ms   | Checks the UART for single-key commands      |

All tasks live in a fixed table; the `scheduler` is given that table at compile time.

Console commands: `h` help, `s` status, `d` dump temperature history, `v` toggle verbose output, `q` quit QEMU.

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
# with Debian/Ubuntu's g++-aarch64-linux-gnu instead:
cmake -B build-gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake -DCROSS_PREFIX=aarch64-linux-gnu-
```

## Tests

Robot Framework suites in `tests/` boot the firmware in QEMU and drive it over the
UART. They cover boot, task timing, the console commands and the quit exit code. They
also check the ELF image statically: load address, no RTTI, exception, heap or guard
symbols, an empty `.init_array`, and a size budget.

```sh
python3 -m venv .venv && source .venv/bin/activate
pip install -r tests/requirements.txt
cmake -B build -G Ninja                       # configure with the venv active
cmake --build build
ctest --test-dir build --output-on-failure    # report: build/robot/log.html
```

Run one suite or test directly:

```sh
robot --pythonpath tests/libraries --outputdir build/robot tests/console.robot
robot --pythonpath tests/libraries --outputdir build/robot --test "Quit*" tests
```

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and on every pull request, in three stages:

| Stage   | Jobs | What it does |
|---------|------|--------------|
| `lint`  | 1    | clang-format check (pinned to 21.1.8) and a Robot Framework dry run |
| `build` | 8    | host {Linux x64, Linux arm64} × {GCC, clang} × {Debug, Release}, built with `-DFIRMWARE_WERROR=ON`; uploads the firmware as an artifact |
| `test`  | 8    | the same matrix; downloads the matching firmware and runs the Robot suites under QEMU, with no compiler installed |

GCC on the x64 host is the `g++-aarch64-linux-gnu` cross compiler. On the arm64 host it's
Ubuntu's native `g++`. Both provide `aarch64-linux-gnu-*` binaries, so both use the same
`CROSS_PREFIX`. Robot logs are uploaded as artifacts too.

### Debugging

```sh
cmake --build build --target debug  # QEMU halts and waits for a debugger on :1234
lldb build/firmware.elf -o "gdb-remote 1234"
```

## Layout

```
CMakeLists.txt                      build flags, objcopy to .bin, run/debug targets, ctest
.github/workflows/ci.yml            GitHub Actions pipeline
tests/*.robot                       Robot Framework suites (boot, tasks, console, image)
tests/libraries/                    Python keyword libraries: QEMU/UART driver, ELF checks
cmake/toolchain-clang-aarch64.cmake clang++ + lld toolchain (default)
cmake/toolchain-gcc-aarch64.cmake   aarch64-none-elf-g++ toolchain
linker/qemu_virt.ld                 memory map: RAM at 0x40000000, .init_array, stack, .bss
src/startup.S                       _start: park secondary cores, set up the stack, enable FP, zero .bss, run constructors, call main
src/main.cpp                        static object graph + main loop
src/app/scheduler.*                 task base class (virtual run) + cooperative scheduler
src/app/tasks.*                     led, temperature_sensor, heartbeat/sensor/console tasks
src/app/console.*                   type-safe `console << ...` output (replaces printf)
src/hal/mmio.hpp                    Reg32 register wrapper, BitField<Shift, Width>
src/hal/pl011.*                     PL011 UART driver (0x09000000)
src/hal/generic_timer.*             ARM generic timer (CNTPCT_EL0)
src/hal/semihosting.*               semihosting SYS_EXIT so QEMU can quit
src/runtime/cxx_runtime.cpp         __cxa_pure_virtual, global-constructor loop
src/util/ring_buffer.hpp            RingBuffer<T, N> fixed-capacity FIFO
src/util/moving_average.hpp         MovingAverage<T, N, Acc> + Arithmetic concept
src/util/static_tests.cpp           compile-time unit tests
```
