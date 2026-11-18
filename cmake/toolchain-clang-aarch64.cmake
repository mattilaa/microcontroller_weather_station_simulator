# CMake toolchain file: LLVM/Clang + LLD targeting bare-metal AArch64.
#
# Usage:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clang-aarch64.cmake

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Prefer Homebrew LLVM (Apple's clang lacks llvm-objcopy and friends).
find_program(_BREW brew)
if(_BREW)
    execute_process(COMMAND ${_BREW} --prefix llvm
                    OUTPUT_VARIABLE _LLVM_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
endif()
set(LLVM_HINTS ${_LLVM_PREFIX}/bin /usr/local/opt/llvm/bin /usr/bin)

find_program(CMAKE_CXX_COMPILER clang++      HINTS ${LLVM_HINTS} NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_ASM_COMPILER clang        HINTS ${LLVM_HINTS} NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_OBJCOPY      llvm-objcopy HINTS ${LLVM_HINTS} REQUIRED)
find_program(CMAKE_SIZE         llvm-size    HINTS ${LLVM_HINTS} REQUIRED)

set(TARGET_TRIPLE aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET ${TARGET_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${TARGET_TRIPLE})

set(CPU_FLAGS "-mcpu=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib -static")

# Don't try to link a test executable (there is no libc / crt0).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
