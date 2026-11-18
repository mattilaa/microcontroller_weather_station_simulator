cmake_minimum_required(VERSION 3.20)

# Default to the clang toolchain if none was given.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-clang-aarch64.cmake)
endif()

project(baremetal_aarch64 CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/qemu_virt.ld)

add_executable(firmware
    src/startup.S
    src/main.cpp
    src/app/console.cpp
    src/app/scheduler.cpp
    src/app/tasks.cpp
    src/hal/pl011.cpp
    src/hal/generic_timer.cpp
    src/hal/semihosting.cpp
    src/runtime/cxx_runtime.cpp
    src/util/static_tests.cpp   # compile-time tests only, emits no code
)
set_target_properties(firmware PROPERTIES SUFFIX ".elf")
target_include_directories(firmware PRIVATE src)

target_compile_options(firmware PRIVATE
    # Freestanding: no hosted C/C++ library.
    -ffreestanding
    -fno-builtin
    $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>
    # Embedded C++ subset.
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    # Keep the compiler from using FP/SIMD registers.
    -mgeneral-regs-only
    # Let the linker drop unused code.
    -ffunction-sections
    -fdata-sections
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor
    $<$<CONFIG:Debug>:-Og -g3>
)

target_link_options(firmware PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
)
set_target_properties(firmware PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# Raw binary image + size report.
add_custom_command(TARGET firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware> ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware>
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
    COMMENT "Creating firmware.bin"
)

# ---- QEMU helpers ---------------------------------------------------------
find_program(QEMU qemu-system-aarch64)
if(QEMU)
    set(QEMU_ARGS -M virt -cpu cortex-a53 -m 128M -nographic -semihosting
                  -kernel $<TARGET_FILE:firmware>)

    # cmake --build build --target run      (quit: press 'q', or Ctrl-A X)
    add_custom_target(run
        COMMAND ${QEMU} ${QEMU_ARGS}
        DEPENDS firmware USES_TERMINAL
        COMMENT "Running firmware in QEMU")

    # cmake --build build --target debug    (then: gdb/lldb -> target remote :1234)
    add_custom_target(debug
        COMMAND ${QEMU} ${QEMU_ARGS} -S -gdb tcp::1234
        DEPENDS firmware USES_TERMINAL
        COMMENT "QEMU waiting for debugger on :1234")
endif()
