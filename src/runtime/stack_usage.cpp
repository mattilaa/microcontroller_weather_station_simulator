#include "runtime/stack_usage.hpp"

#include <stdint.h>

// Linker script symbols (linker/qemu_virt.ld).
extern "C" uint64_t __stack_bottom[];
extern "C" uint64_t __stack_top[];

namespace runtime
{

namespace
{
constexpr uint64_t PATTERN = 0x5441'504b'4341'5453; // "STACKPAT", startup.S
} // namespace

size_t stack_size()
{
    return static_cast<size_t>(__stack_top - __stack_bottom) * sizeof(uint64_t);
}

size_t stack_peak()
{
    const volatile uint64_t* word = __stack_bottom;
    while(word < __stack_top && *word == PATTERN)
        ++word;
    return static_cast<size_t>(__stack_top - word) * sizeof(uint64_t);
}

} // namespace runtime
