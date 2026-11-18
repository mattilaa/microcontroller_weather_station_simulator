#include "app/scheduler.hpp"

namespace app
{

void scheduler::poll(uint64_t now_ms)
{
    for(task* t : *this)
    {
        if(now_ms >= t->_next_run_ms)
        {
            t->_next_run_ms = now_ms + t->_period_ms;
            ++t->_runs;
            t->run(now_ms);
        }
    }
}

} // namespace app
