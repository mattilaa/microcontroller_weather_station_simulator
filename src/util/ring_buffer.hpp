#pragma once

#include <stddef.h>

namespace util
{

// Fixed-capacity FIFO stored inline (no heap). Capacity is a template
// parameter, so the storage size is known at compile time and the index
// wrap-around compiles down to a bit mask.
template <typename T, size_t N>
class ring_buffer
{
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t capacity()
    {
        return N;
    }

    constexpr size_t size() const
    {
        return _count;
    }

    constexpr bool empty() const
    {
        return _count == 0;
    }

    constexpr bool full() const
    {
        return _count == N;
    }

    // Append to the back; fails (returns false) when full.
    constexpr bool push(const T& value)
    {
        if(full())
            return false;
        _data[wrap(_head + _count)] = value;
        ++_count;
        return true;
    }

    // Remove from the front; fails (returns false) when empty.
    constexpr bool pop(T& out)
    {
        if(empty())
            return false;
        out = _data[_head];
        _head = wrap(_head + 1);
        --_count;
        return true;
    }

    // Element i, counted from the oldest (0) to the newest (size() - 1).
    constexpr const T& operator[](size_t i) const
    {
        return _data[wrap(_head + i)];
    }

    constexpr const T& back() const
    {
        return (*this)[_count - 1];
    }

private:
    static constexpr size_t wrap(size_t i)
    {
        return i & (N - 1);
    }

    T _data[N]{};
    size_t _head = 0;
    size_t _count = 0;
};

} // namespace util
