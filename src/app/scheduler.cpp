#include "app/scheduler.hpp"

extern "C" void scheduler_poll__ptr_u8_u64_u64(
    app::scheduler*,
    uint64_t,
    uint64_t);

extern "C" app::task* app_task_at(
    const app::scheduler* scheduler,
    uint64_t index)
{
    return scheduler->begin()[index];
}

extern "C" void app_run_task(app::task* task, uint64_t now_ms)
{
    task->run_if_due(now_ms);
}

namespace app
{
void scheduler::poll(uint64_t now_ms)
{
    scheduler_poll__ptr_u8_u64_u64(this, _count, now_ms);
}
} // namespace app
