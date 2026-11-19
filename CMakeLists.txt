cmake_minimum_required(VERSION 3.20)

# Default to the clang toolchain if none was given.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-clang-aarch64.cmake)
endif()

project(baremetal_aarch64 CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(FIRMWARE_WERROR "Treat compiler warnings as errors" OFF)

set(Python3_FIND_VIRTUALENV FIRST)
include(cmake/mlang.cmake)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/qemu_virt.ld)

add_executable(firmware
    ${MLANG_OBJECT}
    src/startup.S
    src/runtime/abi.S
)
set_target_properties(firmware PROPERTIES SUFFIX ".elf" LINKER_LANGUAGE CXX)
add_dependencies(firmware mlang_commands)
target_include_directories(firmware PRIVATE src)

target_compile_options(firmware PRIVATE
    # Freestanding: no hosted C/C++ library.
    -ffreestanding
    -fno-builtin
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    # Keep the compiler from using FP/SIMD registers.
    -mgeneral-regs-only
    # The MMU is off, so all memory is Device memory: unaligned accesses
    # fault. Never let the compiler merge byte accesses into unaligned ones.
    -mstrict-align
    # Let the linker drop unused code.
    -ffunction-sections
    -fdata-sections
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<BOOL:${FIRMWARE_WERROR}>:-Werror>
)

target_link_options(firmware PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
    -Wl,--print-memory-usage   # FLASH / RAM use against the 32 KiB budget
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
    # Network: virtio-net on QEMU's user-mode network. Host UDP port
    # ${QEMU_UDP_PORT} is forwarded to the weather service (port 5000):
    #   echo weather | nc -u -w1 127.0.0.1 5555
    # -nic none drops the default PCI NIC (it needs efi-virtio.rom).
    set(QEMU_UDP_PORT 5555 CACHE STRING "Host UDP port forwarded to the firmware")
    # Simulation speed switch: 1 = real time, 600 = ten simulated minutes per
    # second (max 72000). cmake -B build -DSIM_SPEED=600
    set(SIM_SPEED 1 CACHE STRING "Simulated seconds per real second (1..72000)")
    # Weather scenario: default, summer_heat, winter_high, storm, thunderstorm.
    # cmake -B build -DSIM_SCENARIO=storm
    set(SIM_SCENARIO default CACHE STRING "Weather scenario")
    set(QEMU_ARGS -M virt -cpu cortex-a53 -m 128M -nographic -semihosting
                  -nic none
                  -global virtio-mmio.force-legacy=false
                  -netdev user,id=net0,hostfwd=udp:127.0.0.1:${QEMU_UDP_PORT}-:5000
                  -device virtio-net-device,netdev=net0
                  -fw_cfg name=opt/weather/speed,string=${SIM_SPEED}
                  -fw_cfg name=opt/weather/scenario,string=${SIM_SCENARIO}
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

# ---- Robot Framework tests ------------------------------------------------
# ctest --test-dir build --output-on-failure
# Uses the active virtualenv first (pip install -r tests/requirements.txt).
enable_testing()
set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME mlang_units
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_mlang.py"
                --compiler-root "${MLANG_ROOT}")
    set_tests_properties(mlang_units PROPERTIES LABELS "mlang;unit")
endif()
if(Python3_FOUND AND QEMU)
    add_test(NAME robot
        COMMAND ${Python3_EXECUTABLE} -m robot
                --pythonpath ${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries
                --variable FIRMWARE:$<TARGET_FILE:firmware>
                --variable QEMU:${QEMU}
                --outputdir ${CMAKE_CURRENT_BINARY_DIR}/robot
                --consolecolors off
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
