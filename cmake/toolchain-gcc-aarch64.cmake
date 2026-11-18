# CMake toolchain file: GNU Arm toolchain (aarch64-none-elf-gcc) for bare-metal AArch64.
# Alternative to the clang toolchain; get it from Arm GNU Toolchain downloads.
#
# Usage:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_PREFIX aarch64-none-elf- CACHE STRING "GNU cross toolchain prefix")

find_program(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++     REQUIRED)
find_program(CMAKE_ASM_COMPILER ${CROSS_PREFIX}gcc     REQUIRED)
find_program(CMAKE_OBJCOPY      ${CROSS_PREFIX}objcopy REQUIRED)
find_program(CMAKE_SIZE         ${CROSS_PREFIX}size    REQUIRED)

set(CPU_FLAGS "-mcpu=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -static")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
