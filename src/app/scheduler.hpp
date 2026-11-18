#pragma once

#include <stddef.h>
#include <stdint.h>

namespace app
{

// A periodic job. Derive from it and implement run().
class task
{
public:
    constexpr task(const char* name, uint32_t period_ms)
        : _name(name), _period_ms(period_ms)
    {
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    virtual void run(uint64_t now_ms) = 0;

    // Call run() if the task's deadline has passed, then schedule the next
    // run one period later. Returns true if the task ran.
    bool run_if_due(uint64_t now_ms)
    {
        if(now_ms < _next_run_ms)
            return false;
        _next_run_ms = now_ms + _period_ms;
        ++_runs;
        run(now_ms);
        return true;
    }

    const char* name() const
    {
        return _name;
    }

    uint32_t period_ms() const
    {
        return _period_ms;
    }

    uint32_t runs() const
    {
        return _runs;
    }

protected:
    // Non-virtual and protected: tasks are never deleted through a task*,
    // so no virtual destructor (and no operator delete) is needed.
    // Don't give task friends: GCC's -Wnon-virtual-dtor treats a protected
    // destructor as accessible when the class has friends.
    ~task() = default;

private:
    const char* _name;
    uint32_t _period_ms;
    uint64_t _next_run_ms = 0;
    uint32_t _runs = 0;
};

// Cooperative scheduler over a fixed task table (no dynamic allocation).
class scheduler
{
public:
    template <size_t N>
    explicit constexpr scheduler(task* const (&tasks)[N])
        : _tasks(tasks), _count(N)
    {
    }

    // Run every task whose deadline has passed.
    void poll(uint64_t now_ms);

    task* const* begin() const
    {
        return _tasks;
    }

    task* const* end() const
    {
        return _tasks + _count;
    }

private:
    task* const* _tasks;
    size_t _count;
};

} // namespace app
