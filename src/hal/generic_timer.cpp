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

void GenericTimer::init()
{
    ticks_per_ms_ = read_cntfrq() / 1000;
    start_ticks_ = read_cntpct();
}

uint64_t GenericTimer::millis() const
{
    return (read_cntpct() - start_ticks_) / ticks_per_ms_;
}

void GenericTimer::delay_ms(uint32_t ms) const
{
    const uint64_t end = millis() + ms;
    while(millis() < end)
    {
    }
}

} // namespace hal
