// Compile-time unit tests for the template utilities.
//
// Everything here is evaluated by the compiler: if a test fails, the build
// fails. Nothing ends up in the firmware image.

#include <stdint.h>

#include "hal/mmio.hpp"
#include "util/moving_average.hpp"
#include "util/ring_buffer.hpp"

namespace
{

// ---- ring_buffer
// -------------------------------------------------------------

constexpr bool ring_buffer_fifo_order()
{
    util::ring_buffer<int, 4> rb;
    rb.push(1);
    rb.push(2);
    rb.push(3);
    int v = 0;
    rb.pop(v);
    return v == 1 && rb.size() == 2 && rb[0] == 2 && rb.back() == 3;
}

static_assert(ring_buffer_fifo_order());

constexpr bool ring_buffer_rejects_when_full()
{
    util::ring_buffer<int, 2> rb;
    return rb.push(1) && rb.push(2) && !rb.push(3) && rb.full();
}

static_assert(ring_buffer_rejects_when_full());

constexpr bool ring_buffer_wraps_around()
{
    util::ring_buffer<int, 2> rb;
    int v = 0;
    for(int i = 0; i < 5; ++i) // head and tail cross the end several times
    {
        rb.push(i);
        rb.pop(v);
    }
    return v == 4 && rb.empty();
}

static_assert(ring_buffer_wraps_around());

constexpr bool ring_buffer_pop_empty_fails()
{
    util::ring_buffer<int, 2> rb;
    int v = 42;
    return !rb.pop(v) && v == 42;
}

static_assert(ring_buffer_pop_empty_fails());

static_assert(util::ring_buffer<char, 16>::capacity() == 16);

// util::ring_buffer<int, 3> bad; // error: N must be a power of two

// ---- moving_average
// ----------------------------------------------------------

constexpr bool moving_average_window()
{
    util::moving_average<int32_t, 4, int64_t> avg;
    avg.add(10); // [10]          -> 10
    avg.add(20); // [10 20]       -> 15
    if(avg.average() != 15)
        return false;
    avg.add(30);
    if(avg.add(40) != 25) // [10 20 30 40] -> 25
        return false;
    return avg.add(50) == 35; // [20 30 40 50] -> 35, oldest dropped
}

static_assert(moving_average_window());

constexpr bool moving_average_wide_accumulator()
{
    // The sum (4 * 2'000'000'000) overflows int32_t but fits in int64_t.
    util::moving_average<int32_t, 4, int64_t> avg;
    for(int i = 0; i < 4; ++i)
        avg.add(2'000'000'000);
    return avg.average() == 2'000'000'000;
}

static_assert(moving_average_wide_accumulator());

static_assert(util::moving_average<int32_t, 4>{}.average() == 0);

// The concept rejects types without arithmetic:
struct not_a_number
{
};

static_assert(util::arithmetic<int32_t>);
static_assert(!util::arithmetic<not_a_number>);
static_assert(!util::arithmetic<const char*>);

// ---- bit_field
// ---------------------------------------------------------------

using wlen = hal::bit_field<5, 2>;
static_assert(wlen::MASK == 0b0110'0000);
static_assert(wlen::value<3>() == 0b0110'0000);
static_assert(wlen::get(0xFF) == 3);
static_assert(wlen::set(0xFFFF'FFFF, 1) == 0xFFFF'FFBF);
// wlen::value<4>();  // error: value does not fit in field
// hal::bit_field<31, 2>{}; // error: field exceeds 32 bits

} // namespace
