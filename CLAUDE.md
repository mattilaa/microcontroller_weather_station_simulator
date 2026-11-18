# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Bare-metal AArch64 firmware (no OS) in freestanding C++20, run on QEMU `virt` (Cortex-A53), built with CMake.

## Commands

```sh
cmake -B build -G Ninja                 # configure (clang++/lld toolchain is the default)
cmake --build build                     # build: firmware.elf, firmware.bin, firmware.map
cmake --build build --target run        # run in QEMU; press 'q' to exit (semihosting), or Ctrl-A X
cmake --build build --target debug      # QEMU halts and waits for a debugger on :1234
cmake -B build-gcc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake -DCROSS_PREFIX=aarch64-linux-gnu-   # GCC (as in CI)
cmake -B build -DFIRMWARE_WERROR=ON     # warnings as errors (CI always sets this)
```

Format check (the repo has a `.clang-format`: Allman braces, 80 columns). Under zsh, pass files via `find | xargs`, not an unquoted variable:
```sh
find src \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
```

**Tests** come in two kinds:
- **Compile-time:** `src/util/static_tests.cpp` holds `static_assert`s on `constexpr` code; a successful build means they pass.
- **Robot Framework** (`tests/*.robot`): boots the firmware in QEMU and drives the UART, plus static ELF checks in `image.robot`. They need the venv: `python3 -m venv .venv && .venv/bin/pip install -r tests/requirements.txt`. Configure CMake with the venv activated, so `find_package(Python3)` picks it up.

```sh
ctest --test-dir build --output-on-failure                     # all suites; log in build/robot/log.html
robot --pythonpath tests/libraries --outputdir build/robot tests/console.robot           # one suite
robot --pythonpath tests/libraries --outputdir build/robot --test "Quit*" tests          # one test
robot --pythonpath tests/libraries --variable FIRMWARE:build-release/firmware.elf ...    # another build
```

Robot test notes:
- Two or more spaces separate arguments in `.robot` files, so write `{2}` in regexes instead of literal double spaces.
- `qemu_firmware.py` reads output through a cursor: `Wait For Output` only sees text after the previous match, and `Collect Output` moves the cursor to the end.
- QEMU is always run with `-nic none`: the default virtio NIC needs `efi-virtio.rom`, which CI's minimal QEMU install doesn't ship (QEMU exits at startup).
- Timing checks allow wide tolerances (e.g. 450–650 ms for the 500 ms heartbeat) because CI runners stall QEMU.
- The `Image Fits Size Budget` test in `image.robot` caps code + rodata + data at 16 KiB.

**CI** (`.github/workflows/ci.yml`) runs three stages:
- **`lint`:** clang-format pinned to 21.1.8 via pip, plus `robot --dryrun`.
- **`build`:** host {x64 `ubuntu-24.04`, arm64 `ubuntu-24.04-arm`} × {gcc, clang} × {Debug, Release}, built with `-DFIRMWARE_WERROR=ON`. It uploads `firmware-<host>-<toolchain>-<build_type>` artifacts.
- **`test`** (`needs: build`): has no compiler. It downloads the matching artifact into `build/` and runs `robot` directly (not ctest), on the same host arch.

The `build` and `test` matrices must be kept identical, because artifact names link them.

GCC details:
- GCC uses `CROSS_PREFIX=aarch64-linux-gnu-` on both hosts. On x64 that's the apt `g++-aarch64-linux-gnu` cross compiler; on arm64 it's the native `g++`, which ships the same prefixed binary names.
- The GCC toolchain file adds `-fno-pie -fno-stack-protector -no-pie`, because distro GCC enables PIE and stack protector by default. Both break bare-metal linking.

**Non-interactive run** (e.g. to check behavior from a script). The firmware reads single-key commands from the UART: `h s d v q`. macOS has no `timeout`, so put a kill-based watchdog around QEMU:
```sh
(sleep 2; printf 's'; sleep 0.3; printf 'q'; sleep 2) | qemu-system-aarch64 -M virt -cpu cortex-a53 -m 128M -nographic -nic none -semihosting -kernel build/firmware.elf & P=$!; sleep 8; kill $P 2>/dev/null; wait $P
```

## Embedded C++ constraints (enforced by flags in CMakeLists.txt)

- `-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`, no unwind tables, `-mgeneral-regs-only`.
- `-ffreestanding -nostdinc++ -nostdlib`: no libc or libc++. Only compiler-provided C headers (`<stdint.h>`, `<stddef.h>`, `<stdarg.h>`) are available. There's no `<type_traits>`, `<array>`, `<concepts>` etc., so write the needed bits yourself (see the `util::arithmetic` concept).
- `operator new`/`delete` are deliberately undefined, so heap use is a link error. Don't add them to "fix" a link error; remove the allocation instead.
- No `memcpy`/`memset` are provided. If the compiler starts emitting calls to them (e.g. large struct copies), the link fails.
- Polymorphic bases (e.g. `app::task`) use a **protected non-virtual destructor**. A virtual destructor would pull in `operator delete`. Don't give such classes `friend`s: GCC's `-Wnon-virtual-dtor` then counts the protected destructor as accessible and warns, and CI builds with `-Werror`.
- Warnings include `-Wpedantic -Wconversion -Wshadow`. Keep the build free of warnings.

## Architecture

**Boot path:** QEMU loads the ELF at `0x40000000` (`linker/qemu_virt.ld`) and enters `_start` in `src/startup.S` at EL1 on every core. Core 0 sets up the stack, enables FP, zeroes `.bss`, calls `__run_global_constructors` (`src/runtime/cxx_runtime.cpp`, walks `.init_array`), then `main`. `main` is `extern "C"`, because under `-ffreestanding` it would otherwise be name-mangled.

**Static object graph:** `src/main.cpp` declares every object (`serial_console`, `status_led`, `temp_sensor`, the tasks, `task_table`, `task_scheduler`) as a `constinit` global in an anonymous namespace. They're wired together through constructor references, so `.init_array` stays empty. New globals should stay `constinit`, which means they need `constexpr` constructors. `command_shell` (a `console_task`) needs a reference to `task_scheduler`, which is defined later, hence the `extern scheduler task_scheduler;` forward declaration.

**Main loop:** `task_scheduler.poll(timer.millis())` in a busy loop. `app::scheduler` holds a fixed `task* const` table; its template constructor works out the table size. Each `app::task` has a name and a period, and overrides `run(now_ms)`. Scheduling bookkeeping (`_next_run_ms`, `_runs`) is private to `task`; `scheduler` calls the public non-virtual `task::run_if_due()`, which updates it and calls `run()`. To add a task: subclass `task` in `src/app/tasks.*` (mark it `final`, give it a `constexpr` constructor), create a `constinit` instance in `main.cpp` and add it to `task_table`.

**Layers:**
- `hal::` is hardware access only. `reg32`/`bit_field<Shift,Width>`/`flag<Bit>` (`mmio.hpp`) are used to define registers; the PL011 driver shows the pattern. Peripheral base addresses are QEMU virt's (UART0 `0x09000000`, `hal::uart0`). Timing comes from polling the ARM generic timer; no interrupts or GIC are set up.
- `app::` holds application logic. `app::console` is a type-safe `operator<<` stream over a `pl011` that replaces printf. Formatting goes through `dec(v, width, pad)`/`hex(...)`. Domain types add their own `operator<<(console&, T)` (e.g. `centi_celsius`).
- `util::` holds header-only, `constexpr` templates (`ring_buffer`, `moving_average`). Add `static_assert` tests to `src/util/static_tests.cpp` when changing them.

**Exit/panic:** `hal::semihosting::exit(code)` ends QEMU only because the run targets pass `-semihosting`. Without that flag it just halts. `__cxa_pure_virtual` panics through the same path with exit code 1.

## Code conventions

Naming:

| Kind                                  | Style                  | Example                              |
|---------------------------------------|------------------------|--------------------------------------|
| Data members (class and struct)       | `_` + `snake_case`     | `_member_name`, `_period_ms`, `_next_run_ms` |
| Locals and function parameters        | `snake_case`, no prefix | `now_ms`, `delta`                   |
| Functions and methods                 | `snake_case`           | `try_get()`, `print_status()`        |
| Types (classes, structs, aliases, concepts) | `snake_case`     | `ring_buffer`, `generic_timer`, `arithmetic` |
| Template parameters                   | `PascalCase` / single letter | `T`, `N`, `Shift`, `Width`     |
| Constants (`constexpr`, register offsets/fields, enumerators) | `UPPER_SNAKE_CASE` | `HISTORY`, `FR_TXFF` |
| Namespaces, files and directories     | lowercase `snake_case` | `hal::semihosting`, `generic_timer.cpp` |

- Types and functions share the `snake_case` style, so a type can't have the same name as a function, variable or member in the same scope. Pick distinct names: e.g. the type `dec_fmt` is made by the function `dec()`, and the global `serial_console` has type `console`. Avoid declarations like `task* task`.
- Only use the leading underscore for members. Never use it at namespace scope, and never follow it with an uppercase letter: `_Name` and global `_name` are reserved identifiers in C++.
- Public plain-data structs that are used with aggregate initialization (e.g. `dec_fmt{value, width, pad}`, `centi_celsius{value}`) are the one exception: their fields have no prefix.
- Accessors are named after the property with no `get_` prefix: `name()` returns `_name`.
- Run clang-format after editing; the repo's `.clang-format` decides layout.

## Repo conventions

- Commit messages: a descriptive title and a bullet-point body. Don't mention Claude or add AI co-author trailers.
- New source files must be added to the `add_executable` list in `CMakeLists.txt` (it doesn't glob).
- `compile_commands.json` in the root is a symlink into `build/` (gitignored) and exists for clangd.
