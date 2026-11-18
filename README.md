[![CI](https://github.com/mattilaa/microcontroller_example/actions/workflows/ci.yml/badge.svg)](https://github.com/mattilaa/microcontroller_example/actions/workflows/ci.yml)

# Microcontroller example

A bare-metal AArch64 program (no operating system) that runs a microcontroller-style
main loop on QEMU's `virt` machine (Cortex-A53). It is written in freestanding C++20
and built with CMake.

It's a small simulated **weather station**: temperature, humidity, wind speed, wind
direction, rain, cloud cover and cloud layer sensors read a simulated weather model. Readings are printed on the
UART, logged, and served over **Ethernet**, through a virtio-net driver and a minimal
IPv4 stack (ARP, ping, UDP). A Python tool imports the log and plots it in the style of
the Finnish Meteorological Institute's forecast chart:

![Two simulated days](docs/weather_chart.png)

**Just the commands** for setup, running and testing: [howto.txt](howto.txt).

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
| `weather`   | 1000 ms | Samples all five sensors; logs a record every 10 simulated minutes |
| `network`   | 5 ms    | Polls the virtio-net device and answers ARP, ping and UDP |
| `console`   | 10 ms   | Checks the UART for single-key commands      |

All tasks live in a fixed table; the `scheduler` is given that table at compile time.

Console commands: `h` help, `s` status, `w` weather report, `d` dump the last 8 records,
`n` network status, `+`/`-` faster/slower simulation, `v` toggle verbose output, `q` quit
QEMU.

## Real time and the speed switch

By default the simulation runs in **real time**: the station samples its sensors every
second, and simulated time passes as fast as real time. Day 1 starts at 06:00.

Like a real automatic weather station, it logs **10-minute records**. Each record holds
the time-weighted means of the samples in its interval (wind direction averaged on the
circle) and the rain that fell in it, and is stamped with the interval's end time: the
06:10 record covers 06:00–06:10.

To avoid waiting 48 hours for 48 hours of data, set the **speed switch** to any value from
1 to 72000 (simulated seconds per real second):

| Speed | One real second is | 48 simulated hours take |
|-------|--------------------|-------------------------|
| 1 (default) | 1 s (real time) | 48 hours |
| 600 | 10 minutes | under 5 minutes |
| 3600 | 1 hour | under a minute |
| 72000 | 20 hours | under 3 seconds |

- **QEMU command line:** `-fw_cfg name=opt/weather/speed,string=600`. The firmware reads
  it at boot through QEMU's fw_cfg device.
- **CMake `run` target:** `cmake -B build -DSIM_SPEED=600`.
- **While running:** the UDP command `speed 600`, or the console keys `+`/`-` (1, 10, 60,
  600, 3600, 72000).
- **Plotting tool:** `record --speed 600`.

The weather model steps once per simulated minute at every speed, so the weather
behaves the same at 1× and at 72000×. Every sample is still taken from the model
(at most one simulated minute apart). At 72000× each one-second update simulates 1200
minutes, which takes the firmware a few milliseconds, so the console and network stay
responsive.

At 72000× the station logs 120 records per second, and its 256-record log wraps in about
2 seconds. `record` picks its import interval from the speed (0.53 s at 72000×), so it
keeps up. `fetch`, and `record` with a too-long `--interval`, warn when records are lost.

## Memory: 32 KiB of RAM, code in flash

The firmware is laid out like a microcontroller with **32 KiB of RAM**:
- **Flash:** code, constants and the initial values of `.data`.
- **RAM:** only `.data`, `.bss` and the stack.
- **Boot:** `startup.S` copies `.data` from flash to RAM, as on a real chip. QEMU `virt` has
  no flash for a bare `-kernel` image, so the flash region is the start of QEMU's RAM, used
  as read-only image memory.
- **Enforced by the linker:** a build that needs more RAM fails with
  `section '.bss' will not fit in region 'RAM'`. Every link prints the use:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:       31200 B         1 MB      2.98%
             RAM:       26688 B        32 KB     81.45%
```

| RAM user | Bytes |
|----------|-------|
| Ethernet: 4 receive + 2 transmit buffers of 1526 B, plus rings | 9 400 |
| Reading log: 256 records, packed into 36 B each | 9 230 |
| IP stack frame copies (receive + transmit), in `.data` | 3 090 |
| Weather model, sensors, tasks, console | ~700 |
| Stack | 4 096 |

The stack is filled with a pattern at boot, and the console's `s` status shows the peak use
since then. After the deepest code paths (console reports, UDP log pages, a 1400-byte ping),
it peaks at about 870 of 4096 bytes. A test checks it stays under half.

## Weather simulation

| Quantity | Model |
|----------|-------|
| Temperature | daily sine cycle, 15 °C ± 8 °C (coldest ~03:00, warmest ~15:00), plus a slowly drifting "weather front" offset of up to ±3 °C |
| Humidity | falls about 3 %RH per °C of warming (relative humidity drops as air warms), plus its own slow moisture drift |
| Wind speed | pulled toward a daily mean (1–5 m/s, windiest in the afternoon) with random variation, plus occasional gusts that decay |
| Wind direction | a prevailing direction (starting SW) that drifts slowly; the vane swings ~30° in calm air but only a few degrees in strong wind |
| Cloud cover | low and middle cloud follows a slowly wandering trend; showers build and clear over hours. The sky imager reports total cover in whole percent, counting thin cirrus half |
| Cloud layers | each type at its own level: **cirrus** high up (centred near 270 hPa / 10 km, drifting up and down, thicker when there's more of it); **altostratus**, a middle sheet (centred near 580 hPa / 4.5 km, also drifting and thickening, and its base sinks in steady rain); **cumulus** from the cloud base upward, growing with afternoon convection and gone at night; **cumulonimbus**, in heavy convective rain, from the cloud base right up to the tropopause (200 hPa) |
| Cloud base (LCL) | the lifting condensation level, from humidity: about 125 m per °C of dew-point spread, converted to hPa with a standard-atmosphere table. It's low in humid air and rain, and higher on dry afternoons |
| Rain | falls when cloudiness passes a threshold, harder the cloudier it gets (up to ~5 mm/h); clouds flatten the daily temperature swing, rain cools the air, and humidity builds up during rain and dries out over the following hours |

The slow parts are mean-reverting random processes (Ornstein–Uhlenbeck style). Each
sensor then adds its own noise and rounds to its resolution, as a real instrument
would. The rain gauge is a tipping bucket: it reports rain only in whole 0.2 mm tips. Without floating point, `sin()` is an integer approximation (Bhaskara I).
Wind direction is averaged on the circle, so 350° and 10° average to 0°, not 180°.

## Network

The firmware probes the 32 virtio-mmio slots for a virtio-net device. It needs the
modern virtio interface (`-global virtio-mmio.force-legacy=false`). It uses the static
address 10.0.2.15/24, which matches QEMU's user-mode network. The UDP weather service
listens on port 5000; each datagram is one command:

| Command | Reply |
|---------|-------|
| `weather` | the latest sample: `{"day":1,"time":"06:10:05","temperature":12.34,"humidity":71.20,"wind_speed":3.42,"wind_direction":225,"wind_compass":"SW","rain":0.40,"rain_today":1.20,"cloud_cover":63,"lcl":912,"cirrus":25,"altostratus":20,"cumulus":30,"cumulus_top":650,"cumulonimbus":0,"cirrus_base":300,"cirrus_top":240,"altostratus_base":640,"altostratus_top":520,"speed":1}` |
| `time HH:MM` | `ok time HH:MM`: jumps the simulated clock |
| `log [sequence]` | one page (up to 14 rows) of 10-minute records as CSV, from `sequence`; the header line `log next=N count=C` says where to continue |
| `advance MINUTES` | fast-forwards the simulation instantly (10…4320 minutes), logging the records |
| `speed [N]` | shows or sets the simulation speed (1…72000) |
| `ping` | `pong` |
| `help` | the list of commands |

`cmake --build build --target run` forwards host UDP port 5555 to the service:

```sh
echo weather | nc -u -w1 127.0.0.1 5555
```

The firmware keeps the last 256 records (42.7 simulated hours) in its log.

## Importing and plotting the data

`tools/weather_tool.py` imports the log over UDP into a CSV file and plots it with
matplotlib, in the style of the [FMI forecast chart](https://www.ilmatieteenlaitos.fi/saa/helsinki?forecast=daily):

- **Top panel:** temperature (red), dew point (green, dotted, computed from temperature and
  humidity), and hourly precipitation bars on the right-hand mm axis.
- **Cloud panel** (with cloud layer data): a time-height cross-section on a log-pressure
  axis, 1000 hPa at the bottom to 200 hPa at the top, with km on the right. Each 10-minute
  record is one column, with no smoothing between records. Every cloud type spans its own
  base and top for that record, more opaque the more cloud there is:
  - cirrus is wispy and densest in its middle, drifting around 10 km
  - altostratus is a sheet that undulates, and lowers in steady rain
  - cumulus has a flat base on the LCL and a soft, billowing top
  - cumulonimbus is a dark tower from the cloud base to the tropopause

  The LCL (cloud base) is a dashed line.
- **Bottom panel:** wind speed, with hourly arrows that point where the wind blows to and
  the hourly mean speed under each arrow.
- **Time axis:** a dashed line at each midnight; the temperature panel also has
  alternating hour bands.

With the [virtual environment](#python-virtual-environment) set up and active:

```sh
# Record live: starts its own QEMU, imports records as they are measured and keeps
# weather.png up to date (Ctrl-C stops). --speed is the speed switch.
python tools/weather_tool.py record --speed 3600 --hours 48     # 48 h in under a minute
python tools/weather_tool.py record --speed 72000 --hours 72   # 72 h in ~5 s
python tools/weather_tool.py record --speed 600 --live          # also a live chart window
python tools/weather_tool.py record                             # real time

# Against an already running firmware (cmake --build build --target run):
python tools/weather_tool.py record --port 5555 --speed 600
python tools/weather_tool.py fetch --out weather.csv            # one import
python tools/weather_tool.py plot weather.csv --out weather.png --hours 24

# Instant: fast-forward without waiting at all.
python tools/weather_tool.py demo --firmware build/firmware.elf --minutes 2880 --hours 48
```

The CSV columns are
`seq,day,time,temperature,humidity,wind_speed,wind_direction,rain,cloud_cover,lcl,cirrus,altostratus,cumulus,cumulus_top,cumulonimbus,cirrus_base,cirrus_top,altostratus_base,altostratus_top`
(pressures in hPa, cloud amounts in %). Older CSVs without the newer columns at the end
still plot: without layer levels the cirrus and altostratus fall back to fixed levels, and
without any cloud columns there's no cloud panel.

`fetch` and `record` append only records that aren't in the CSV yet, so they can be run
repeatedly while the firmware keeps measuring. `record` with its own QEMU starts a fresh
CSV, because a fresh firmware starts a fresh log.

## Requirements

- CMake ≥ 3.20 and Ninja (or Make)
- `qemu-system-aarch64`
- A cross compiler, either:
  - LLVM clang++ + lld (default): `brew install llvm lld`
  - or the Arm GNU toolchain `aarch64-none-elf-g++`
- Python 3.11 or newer (CI uses 3.12), for the tests and the plotting tool

## Python virtual environment

Set up a virtual environment before installing any Python packages. That keeps the
project's packages (Robot Framework, pyelftools, matplotlib) out of your system Python,
and pins them to the versions in the `requirements.txt` files. Do this once, in the
repository root:

```sh
python3 -m venv .venv                   # create it (.venv/ is gitignored)
source .venv/bin/activate               # activate it (fish: .venv/bin/activate.fish)
python -m pip install --upgrade pip
pip install -r tests/requirements.txt   # test dependencies + the plotting tool's
```

`tests/requirements.txt` includes `tools/requirements.txt`. If you only want the plotting
tool, `pip install -r tools/requirements.txt` is enough.

- **New terminals:** activate the environment again in every new terminal with
  `source .venv/bin/activate`. Your prompt shows `(.venv)` while it's active, and
  `deactivate` leaves it.
- **CMake:** configure a new build directory with the environment active. CMake records
  which Python `ctest` uses, and Robot Framework is only installed in the virtual
  environment. CMake keeps the Python it found first, so activating the environment and
  re-running `cmake` on an existing build directory doesn't switch it. Either delete the
  build directory and configure again, or point CMake at the environment explicitly:
  `cmake -B build -DPython3_EXECUTABLE=$PWD/.venv/bin/python`.
- **Starting over:** delete `.venv/` and repeat the steps above.

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
| `tasks.robot` | sensor timing, simulated clock rate |
| `console.robot` | every console command, history capping, averages, the quit exit code |
| `weather.robot` | value ranges, daily temperature cycle, humidity vs. temperature, wind behaviour, setting the clock, rain showers and tipping-bucket amounts, cloud cover in rain, cloud layers at their levels, afternoon cumulus, cumulonimbus only in rain-bearing cloud, high and middle layers drifting and thickening, lower cloud base in humid air, daily rain totals, the log and fast-forward |
| `realtime.robot` | real time by default, the QEMU speed switch, the fastest speed (72000×), changing speed over UDP and from the console, records as means of their samples |
| `tools.robot` | `weather_tool.py demo` and `record` end to end (QEMU, import over UDP, plot), recording at 72000× without losing records, and replotting a CSV |
| `ethernet.robot` | gratuitous ARP, ARP replies, ping (incl. large and 40 in a row), UDP commands, dropping corrupted or misaddressed packets |
| `image.robot` | load address, no RTTI, exception, heap or guard symbols, empty `.init_array`, flash size budget, RAM within 32 KiB, `.data` loaded from flash |

For Ethernet, `tests/libraries/network_peer.py` acts as a second host. QEMU's `dgram`
netdev carries each Ethernet frame as a UDP datagram between QEMU and the library. This
needs no root rights or TAP devices, and lets the tests build and check raw frames,
checksums included.

With the [virtual environment](#python-virtual-environment) set up and active:

```sh
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
tests/*.robot                       Robot Framework suites (boot, tasks, console, weather, ethernet, tools, image)
tools/weather_tool.py               import the reading log over UDP, plot it (matplotlib)
tests/libraries/                    Python keyword libraries: QEMU/UART driver, network peer, ELF checks
cmake/toolchain-clang-aarch64.cmake clang++ + lld toolchain (default)
cmake/toolchain-gcc-aarch64.cmake   aarch64-none-elf-g++ toolchain
linker/qemu_virt.ld                 memory map: FLASH at 0x40000000, 32 KiB RAM at 0x40100000
src/startup.S                       _start: park secondary cores, set up the stack, enable FP, zero .bss, run constructors, call main
src/main.cpp                        static object graph + main loop
src/app/scheduler.*                 task base class (virtual run) + cooperative scheduler
src/app/tasks.*                     weather/network/console tasks
src/app/weather_model.*             simulated weather (daily cycles + random processes)
src/app/weather_station.*           sensors (noise, resolution, rain gauge, cloud cover), samples, 10-minute records, log
src/app/weather_service.*           UDP command protocol (port 5000)
src/app/units.*                     fixed-point units and their formatting
src/app/console.*                   UART text output
src/hal/mmio.hpp                    reg32 register wrapper, bit_field<Shift, Width>
src/hal/virtio_mmio.*               virtio-mmio transport, virtqueue<N>
src/hal/virtio_net.*                polled virtio-net driver
src/hal/pl011.*                     PL011 UART driver (0x09000000)
src/hal/generic_timer.*             ARM generic timer (CNTPCT_EL0)
src/hal/semihosting.*               semihosting SYS_EXIT so QEMU can quit
src/hal/fw_cfg.*                    QEMU fw_cfg reader (the speed switch)
src/net/stack.*                     IPv4 stack: ARP, ICMP echo, UDP
src/net/bytes.hpp                   big-endian field access, Internet checksum
src/runtime/cxx_runtime.cpp         __cxa_pure_virtual, global-constructor loop
src/runtime/libc_support.cpp        memcpy, memmove, memset, memcmp
src/runtime/stack_usage.*           stack size and peak use (high-water mark)
src/util/text_stream.*              type-safe `out << ...` formatting to any sink (replaces printf)
src/util/fixed_math.hpp             integer sin/cos, clamp, wrap, rounding
src/util/random.hpp                 xorshift32 with approximate Gaussian noise
src/util/span.hpp                   span<T> buffer view
src/util/ring_buffer.hpp            ring_buffer<T, N> fixed-capacity FIFO
src/util/moving_average.hpp         moving_average<T, N, Acc> + arithmetic concept
src/util/static_tests.cpp           compile-time unit tests
```
