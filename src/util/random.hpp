#pragma once

#include <stdint.h>

namespace util
{

// xorshift32 pseudo-random generator: tiny, fast and deterministic, so the
// simulation produces the same data on every run.
class xorshift32
{
public:
    explicit constexpr xorshift32(uint32_t seed) : _state(seed ? seed : 1u) {}

    constexpr uint32_t next()
    {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        return _state;
    }

    // Uniform integer in [0, n).
    constexpr uint32_t below(uint32_t n)
    {
        return next() % n;
    }

    // Uniform integer in [lo, hi].
    constexpr int32_t between(int32_t lo, int32_t hi)
    {
        return lo +
               static_cast<int32_t>(below(static_cast<uint32_t>(hi - lo + 1)));
    }

    // Approximately normal noise with mean 0 and standard deviation sigma
    // (sum of four uniform samples, Irwin-Hall; bounded at about 3.5 sigma).
    constexpr int32_t gaussian(int32_t sigma)
    {
        int32_t sum = 0;
        for(int i = 0; i < 4; ++i)
            sum += static_cast<int32_t>(below(1000));
        // Four U[0, 1000) samples: mean 1998, standard deviation ~577.
        return (sum - 1998) * sigma / 577;
    }

private:
    uint32_t _state;
};

} // namespace util
