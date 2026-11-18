#include "app/scheduler.hpp"

namespace app
{

void scheduler::poll(uint64_t now_ms)
{
    for(task* t : *this)
        t->run_if_due(now_ms);
}

} // namespace app
