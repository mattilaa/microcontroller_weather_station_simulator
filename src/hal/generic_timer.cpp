#include "hal/generic_timer.hpp"

namespace hal
{

namespace
{
inline uint64_t read_cntfrq()
{
    uint64_t v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

inline uint64_t read_cntpct()
{
    uint64_t v;
    asm volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}
} // namespace

void generic_timer::init()
{
    _ticks_per_ms = read_cntfrq() / 1000;
    _start_ticks = read_cntpct();
}

uint64_t generic_timer::millis() const
{
    return (read_cntpct() - _start_ticks) / _ticks_per_ms;
}

void generic_timer::delay_ms(uint32_t ms) const
{
    const uint64_t end = millis() + ms;
    while(millis() < end)
    {
    }
}

} // namespace hal
