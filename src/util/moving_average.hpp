#pragma once

#include <stddef.h>

#include "util/ring_buffer.hpp"

namespace util
{

// C++20 concept: any type that supports the arithmetic the filter needs.
// Works without the standard library (no <type_traits> / <concepts>).
template <typename T>
concept arithmetic = requires(T a, T b) {
    T{};
    a + b;
    a - b;
    a / b;
};

// Moving average over the last N samples in O(1) per sample: keeps a
// running sum instead of re-adding the whole window.
//
// Acc is the accumulator type; make it wider than T so the sum can't
// overflow (e.g. moving_average<int32_t, 8, int64_t>).
template <arithmetic T, size_t N, arithmetic Acc = T>
class moving_average
{
public:
    // Add a sample and return the new average.
    constexpr T add(T sample)
    {
        if(_window.full())
        {
            T oldest{};
            _window.pop(oldest);
            _sum = _sum - static_cast<Acc>(oldest);
        }
        _window.push(sample);
        _sum = _sum + static_cast<Acc>(sample);
        return average();
    }

    constexpr T average() const
    {
        if(_window.empty())
            return T{};
        return static_cast<T>(_sum / static_cast<Acc>(_window.size()));
    }

    constexpr const ring_buffer<T, N>& samples() const
    {
        return _window;
    }

private:
    ring_buffer<T, N> _window;
    Acc _sum{};
};

} // namespace util
