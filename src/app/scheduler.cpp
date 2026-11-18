#include "app/scheduler.hpp"

namespace app
{

void Scheduler::poll(uint64_t now_ms)
{
    for(Task* task : *this)
    {
        if(now_ms >= task->next_run_ms_)
        {
            task->next_run_ms_ = now_ms + task->period_ms_;
            ++task->runs_;
            task->run(now_ms);
        }
    }
}

} // namespace app
