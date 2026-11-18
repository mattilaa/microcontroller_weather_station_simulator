#pragma once

#include <stddef.h>

#include "util/ring_buffer.hpp"

namespace util
{

// C++20 concept: any type that supports the arithmetic the filter needs.
// Works without the standard library (no <type_traits> / <concepts>).
template <typename T>
concept Arithmetic = requires(T a, T b) {
    T{};
    a + b;
    a - b;
    a / b;
};

// Moving average over the last N samples in O(1) per sample: keeps a
// running sum instead of re-adding the whole window.
//
// Acc is the accumulator type; make it wider than T so the sum can't
// overflow (e.g. MovingAverage<int32_t, 8, int64_t>).
template <Arithmetic T, size_t N, Arithmetic Acc = T>
class MovingAverage
{
public:
    // Add a sample and return the new average.
    constexpr T add(T sample)
    {
        if(window_.full())
        {
            T oldest{};
            window_.pop(oldest);
            sum_ = sum_ - static_cast<Acc>(oldest);
        }
        window_.push(sample);
        sum_ = sum_ + static_cast<Acc>(sample);
        return average();
    }

    constexpr T average() const
    {
        if(window_.empty())
            return T{};
        return static_cast<T>(sum_ / static_cast<Acc>(window_.size()));
    }

    constexpr const RingBuffer<T, N>& samples() const
    {
        return window_;
    }

private:
    RingBuffer<T, N> window_;
    Acc sum_{};
};

} // namespace util
