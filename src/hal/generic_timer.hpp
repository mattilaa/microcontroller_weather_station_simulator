#pragma once

#include <stdint.h>

namespace hal
{

// ARM generic timer (CNTPCT_EL0 / CNTFRQ_EL0), read by polling.
class generic_timer
{
public:
    void init();
    uint64_t millis() const;
    void delay_ms(uint32_t ms) const;

private:
    uint64_t _ticks_per_ms = 1;
    uint64_t _start_ticks = 0;
};

} // namespace hal
