#pragma once

#include <stddef.h>

namespace util
{

// Non-owning view of a contiguous array (a minimal std::span).
template <typename T>
class span
{
public:
    constexpr span() = default;

    constexpr span(T* data, size_t size) : _data(data), _size(size) {}

    template <size_t N>
    constexpr span(T (&array)[N]) : _data(array), _size(N)
    {
    }

    // span<T> converts to span<const T>.
    template <typename U>
        requires requires(U* u) { static_cast<T*>(u); }
    constexpr span(const span<U>& other)
        : _data(other.data()), _size(other.size())
    {
    }

    constexpr T* data() const
    {
        return _data;
    }

    constexpr size_t size() const
    {
        return _size;
    }

    constexpr bool empty() const
    {
        return _size == 0;
    }

    constexpr T& operator[](size_t i) const
    {
        return _data[i];
    }

    constexpr T* begin() const
    {
        return _data;
    }

    constexpr T* end() const
    {
        return _data + _size;
    }

    // Elements [offset, offset + count), clipped to the span.
    constexpr span subspan(
        size_t offset,
        size_t count = static_cast<size_t>(-1)) const
    {
        if(offset > _size)
            offset = _size;
        const size_t rest = _size - offset;
        return span(_data + offset, count < rest ? count : rest);
    }

    constexpr span first(size_t count) const
    {
        return subspan(0, count);
    }

private:
    T* _data = nullptr;
    size_t _size = 0;
};

} // namespace util
