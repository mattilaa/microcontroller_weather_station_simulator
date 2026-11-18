# CMake toolchain file: GNU toolchain for bare-metal AArch64.
# Alternative to the clang toolchain.
#
# Usage (Arm GNU Toolchain, aarch64-none-elf-g++):
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake
# Usage (Debian/Ubuntu g++-aarch64-linux-gnu, as used in CI):
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gcc-aarch64.cmake \
#         -DCROSS_PREFIX=aarch64-linux-gnu-

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_PREFIX aarch64-none-elf- CACHE STRING "GNU cross toolchain prefix")
# CMake re-runs this file inside try_compile() scratch projects while it checks
# the compiler; forward CROSS_PREFIX there, or it falls back to the default.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROSS_PREFIX)

find_program(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++     REQUIRED)
find_program(CMAKE_ASM_COMPILER ${CROSS_PREFIX}gcc     REQUIRED)
find_program(CMAKE_OBJCOPY      ${CROSS_PREFIX}objcopy REQUIRED)
find_program(CMAKE_SIZE         ${CROSS_PREFIX}size    REQUIRED)

# Linux-targeting distro compilers default to PIE and stack protector, which
# need a dynamic loader and libc symbols (__stack_chk_fail); turn them off.
set(CPU_FLAGS "-mcpu=cortex-a53 -fno-pie -fno-stack-protector")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -static -no-pie")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
