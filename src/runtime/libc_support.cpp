// The four memory functions compilers may call on their own, even in
// freestanding code: for large struct copies and zero-initialization, and
// GCC also for loops it recognizes as copies/fills. There is no libc, so
// they are defined here.
//
// Byte loops only: with the MMU off all memory is Device memory, where
// unaligned or DC ZVA tricks would fault. GCC is built with
// -fno-tree-loop-distribute-patterns so it can't turn these loops back into
// calls to themselves.

#include <stddef.h>
#include <stdint.h>

extern "C"
{
    void* memcpy(void* dst, const void* src, size_t n)
    {
        auto* d = static_cast<uint8_t*>(dst);
        const auto* s = static_cast<const uint8_t*>(src);
        for(size_t i = 0; i < n; ++i)
            d[i] = s[i];
        return dst;
    }

    void* memmove(void* dst, const void* src, size_t n)
    {
        auto* d = static_cast<uint8_t*>(dst);
        const auto* s = static_cast<const uint8_t*>(src);
        if(d < s)
            for(size_t i = 0; i < n; ++i)
                d[i] = s[i];
        else
            for(size_t i = n; i != 0; --i)
                d[i - 1] = s[i - 1];
        return dst;
    }

    void* memset(void* dst, int value, size_t n)
    {
        auto* d = static_cast<uint8_t*>(dst);
        for(size_t i = 0; i < n; ++i)
            d[i] = static_cast<uint8_t>(value);
        return dst;
    }

    int memcmp(const void* a, const void* b, size_t n)
    {
        const auto* x = static_cast<const uint8_t*>(a);
        const auto* y = static_cast<const uint8_t*>(b);
        for(size_t i = 0; i < n; ++i)
            if(x[i] != y[i])
                return x[i] < y[i] ? -1 : 1;
        return 0;
    }
}
