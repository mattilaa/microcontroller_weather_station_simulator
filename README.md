[![CI](https://github.com/mattilaa/microcontroller_example/actions/workflows/ci.yml/badge.svg)](https://github.com/mattilaa/microcontroller_example/actions/workflows/ci.yml)

# Microcontroller example

A bare-metal AArch64 program (no operating system) that runs a microcontroller-style
main loop on QEMU's `virt` machine (Cortex-A53). It is written in freestanding C++20
and built with CMake.

It's a small simulated **weather station**: temperature, humidity, wind speed and wind
direction sensors read a simulated weather model. Readings are printed on the UART and
served over **Ethernet**, through a virtio-net driver and a minimal IPv4 stack (ARP,
ping, UDP).

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
| `-mgeneral-regs-only`               | no FP/SIMD at all; quantities are fixed-point integers      |
| `-mstrict-align`                    | the MMU is off, so all memory is Device memory, where unaligned accesses fault |
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
| `hal::virtqueue<N>`               | `src/hal/virtio_mmio.hpp` | virtio ring layout for any queue size      |
| `util::span<T>`                   | `src/util/`        | buffer views for the network code                 |

`src/util/static_tests.cpp` holds compile-time unit tests (`static_assert` on
`constexpr` code). A failing test breaks the build, and the file adds 0 bytes to the image.

`src/runtime/cxx_runtime.cpp` holds the little runtime support that's left:
`__cxa_pure_virtual` (panics) and a loop that calls `.init_array` constructors. The
startup code still runs that loop, in case you add a global that isn't `constinit`.
`src/runtime/libc_support.cpp` provides `memcpy`/`memmove`/`memset`/`memcmp`, which
compilers may call on their own even in freestanding code.

The main loop is a small cooperative scheduler:

| Task        | Period  | What it does                                 |
|-------------|---------|----------------------------------------------|
| `heartbeat` | 500 ms  | Toggles a virtual LED                        |
| `weather`   | 1000 ms | Reads all four sensors; keeps moving averages and a history of 8 readings |
| `network`   | 5 ms    | Polls the virtio-net device and answers ARP, ping and UDP |
| `console`   | 10 ms   | Checks the UART for single-key commands      |

All tasks live in a fixed table; the `scheduler` is given that table at compile time.

Console commands: `h` help, `s` status, `w` weather report, `d` dump weather history,
`n` network status, `v` toggle verbose output, `q` quit QEMU.

## Weather simulation

Simulated time runs 600× faster than real time, so each one-second reading is ten
simulated minutes and a day lasts 2.4 minutes. Day 1 starts at 06:00.

| Quantity | Model |
|----------|-------|
| Temperature | daily sine cycle, 15 °C ± 8 °C (coldest ~03:00, warmest ~15:00), plus a slowly drifting "weather front" offset of up to ±3 °C |
| Humidity | falls about 3 %RH per °C of warming (relative humidity drops as air warms), plus its own slow moisture drift |
| Wind speed | pulled toward a daily mean (1–5 m/s, windiest in the afternoon) with random variation, plus occasional gusts that decay |
| Wind direction | a prevailing direction (starting SW) that drifts slowly; the vane swings ~30° in calm air but only a few degrees in strong wind |

The slow parts are mean-reverting random processes (Ornstein–Uhlenbeck style). Each
sensor then adds its own noise and rounds to its resolution, as a real instrument
would. Without floating point, `sin()` is an integer approximation (Bhaskara I).
Wind direction is averaged on the circle, so 350° and 10° average to 0°, not 180°.

## Network

The firmware probes the 32 virtio-mmio slots for a virtio-net device. It needs the
modern virtio interface (`-global virtio-mmio.force-legacy=false`). It uses the static
address 10.0.2.15/24, which matches QEMU's user-mode network. The UDP weather service
listens on port 5000; each datagram is one command:

| Command | Reply |
|---------|-------|
| `weather` | `{"day":1,"time":"06:10","temperature":12.34,"humidity":71.20,"wind_speed":3.42,"wind_direction":225,"wind_compass":"SW"}` |
| `time HH:MM` | `ok time HH:MM`: jumps the simulated clock |
| `ping` | `pong` |
| `help` | the list of commands |

`cmake --build build --target run` forwards host UDP port 5555 to the service:

```sh
echo weather | nc -u -w1 127.0.0.1 5555
```

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
UART and over Ethernet:

| Suite | Covers |
|-------|--------|
| `boot.robot` | banner, EL1, network bring-up, running without a NIC, rejecting a legacy virtio device |
| `tasks.robot` | heartbeat and sensor timing, simulated clock rate |
| `console.robot` | every console command, history capping, averages, the quit exit code |
| `weather.robot` | value ranges, daily temperature cycle, humidity vs. temperature, wind behaviour, setting the clock |
| `ethernet.robot` | gratuitous ARP, ARP replies, ping (incl. large and 40 in a row), UDP commands, dropping corrupted or misaddressed packets |
| `image.robot` | load address, no RTTI, exception, heap or guard symbols, empty `.init_array`, size budget |

For Ethernet, `tests/libraries/network_peer.py` acts as a second host. QEMU's `dgram`
netdev carries each Ethernet frame as a UDP datagram between QEMU and the library. This
needs no root rights or TAP devices, and lets the tests build and check raw frames,
checksums included.

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
tests/*.robot                       Robot Framework suites (boot, tasks, console, weather, ethernet, image)
tests/libraries/                    Python keyword libraries: QEMU/UART driver, network peer, ELF checks
cmake/toolchain-clang-aarch64.cmake clang++ + lld toolchain (default)
cmake/toolchain-gcc-aarch64.cmake   aarch64-none-elf-g++ toolchain
linker/qemu_virt.ld                 memory map: RAM at 0x40000000, .init_array, stack, .bss
src/startup.S                       _start: park secondary cores, set up the stack, enable FP, zero .bss, run constructors, call main
src/main.cpp                        static object graph + main loop
src/app/scheduler.*                 task base class (virtual run) + cooperative scheduler
src/app/tasks.*                     led and the heartbeat/weather/network/console tasks
src/app/weather_model.*             simulated weather (daily cycles + random processes)
src/app/weather_station.*           sensors (noise, resolution), averages, history
src/app/weather_service.*           UDP command protocol (port 5000)
src/app/units.*                     fixed-point units and their formatting
src/app/console.*                   UART text output
src/hal/mmio.hpp                    reg32 register wrapper, bit_field<Shift, Width>
src/hal/virtio_mmio.*               virtio-mmio transport, virtqueue<N>
src/hal/virtio_net.*                polled virtio-net driver
src/hal/pl011.*                     PL011 UART driver (0x09000000)
src/hal/generic_timer.*             ARM generic timer (CNTPCT_EL0)
src/hal/semihosting.*               semihosting SYS_EXIT so QEMU can quit
src/net/stack.*                     IPv4 stack: ARP, ICMP echo, UDP
src/net/bytes.hpp                   big-endian field access, Internet checksum
src/runtime/cxx_runtime.cpp         __cxa_pure_virtual, global-constructor loop
src/runtime/libc_support.cpp        memcpy, memmove, memset, memcmp
src/util/text_stream.*              type-safe `out << ...` formatting to any sink (replaces printf)
src/util/fixed_math.hpp             integer sin/cos, clamp, wrap, rounding
src/util/random.hpp                 xorshift32 with approximate Gaussian noise
src/util/span.hpp                   span<T> buffer view
src/util/ring_buffer.hpp            ring_buffer<T, N> fixed-capacity FIFO
src/util/moving_average.hpp         moving_average<T, N, Acc> + arithmetic concept
src/util/static_tests.cpp           compile-time unit tests
```
