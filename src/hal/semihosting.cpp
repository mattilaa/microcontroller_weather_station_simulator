#include "hal/semihosting.hpp"

#include <stdint.h>

namespace hal::semihosting
{

namespace
{
constexpr uint64_t SYS_EXIT = 0x18;
constexpr uint64_t ADP_STOPPED_APPLICATION_EXIT = 0x20026;
} // namespace

void exit(int code)
{
    // AArch64 semihosting: HLT #0xF000, w0 = operation, x1 = parameter block.
    const uint64_t block[2] =
        {ADP_STOPPED_APPLICATION_EXIT,
         static_cast<uint64_t>(static_cast<int64_t>(code))};
    register uint64_t x0 asm("x0") = SYS_EXIT;
    register const uint64_t* x1 asm("x1") = block;
    asm volatile("hlt #0xf000" : : "r"(x0), "r"(x1) : "memory");

    for(;;) // not under semihosting: stop here
        asm volatile("wfe");
}

} // namespace hal::semihosting
