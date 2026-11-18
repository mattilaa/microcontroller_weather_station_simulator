#pragma once

#include <stddef.h>

namespace util
{

// Fixed-capacity FIFO stored inline (no heap). Capacity is a template
// parameter, so the storage size is known at compile time and the index
// wrap-around compiles down to a bit mask.
template <typename T, size_t N>
class RingBuffer
{
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t capacity()
    {
        return N;
    }

    constexpr size_t size() const
    {
        return count_;
    }

    constexpr bool empty() const
    {
        return count_ == 0;
    }

    constexpr bool full() const
    {
        return count_ == N;
    }

    // Append to the back; fails (returns false) when full.
    constexpr bool push(const T& value)
    {
        if(full())
            return false;
        data_[wrap(head_ + count_)] = value;
        ++count_;
        return true;
    }

    // Remove from the front; fails (returns false) when empty.
    constexpr bool pop(T& out)
    {
        if(empty())
            return false;
        out = data_[head_];
        head_ = wrap(head_ + 1);
        --count_;
        return true;
    }

    // Element i, counted from the oldest (0) to the newest (size() - 1).
    constexpr const T& operator[](size_t i) const
    {
        return data_[wrap(head_ + i)];
    }

    constexpr const T& back() const
    {
        return (*this)[count_ - 1];
    }

private:
    static constexpr size_t wrap(size_t i)
    {
        return i & (N - 1);
    }

    T data_[N]{};
    size_t head_ = 0;
    size_t count_ = 0;
};

} // namespace util
