#pragma once

#include <stddef.h>
#include <stdint.h>

namespace app
{

// A periodic job. Derive from it and implement run().
class Task
{
public:
    constexpr Task(const char* name, uint32_t period_ms)
        : name_(name), period_ms_(period_ms)
    {
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    virtual void run(uint64_t now_ms) = 0;

    const char* name() const
    {
        return name_;
    }

    uint32_t period_ms() const
    {
        return period_ms_;
    }

    uint32_t runs() const
    {
        return runs_;
    }

protected:
    // Non-virtual and protected: tasks are never deleted through a Task*,
    // so no virtual destructor (and no operator delete) is needed.
    ~Task() = default;

private:
    friend class Scheduler;

    const char* name_;
    uint32_t period_ms_;
    uint64_t next_run_ms_ = 0;
    uint32_t runs_ = 0;
};

// Cooperative scheduler over a fixed task table (no dynamic allocation).
class Scheduler
{
public:
    template <size_t N>
    explicit constexpr Scheduler(Task* const (&tasks)[N])
        : tasks_(tasks), count_(N)
    {
    }

    // Run every task whose deadline has passed.
    void poll(uint64_t now_ms);

    Task* const* begin() const
    {
        return tasks_;
    }

    Task* const* end() const
    {
        return tasks_ + count_;
    }

private:
    Task* const* tasks_;
    size_t count_;
};

} // namespace app
