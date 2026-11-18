#pragma once

#include <stdint.h>

namespace hal
{

// ARM generic timer (CNTPCT_EL0 / CNTFRQ_EL0), read by polling.
class GenericTimer
{
public:
    void init();
    uint64_t millis() const;
    void delay_ms(uint32_t ms) const;

private:
    uint64_t ticks_per_ms_ = 1;
    uint64_t start_ticks_ = 0;
};

} // namespace hal
