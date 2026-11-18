// Compile-time unit tests for the template utilities.
//
// Everything here is evaluated by the compiler: if a test fails, the build
// fails. Nothing ends up in the firmware image.

#include <stdint.h>

#include "hal/mmio.hpp"
#include "net/bytes.hpp"
#include "util/fixed_math.hpp"
#include "util/moving_average.hpp"
#include "util/random.hpp"
#include "util/ring_buffer.hpp"
#include "util/span.hpp"

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

// ---- fixed_math
// ----------------------------------------------------------------

namespace fixed_math_tests
{
using util::sin_deg;
static_assert(sin_deg(0) == 0);
static_assert(sin_deg(30) == 5000);
static_assert(sin_deg(90) == util::SIN_SCALE);
static_assert(sin_deg(180) == 0);
static_assert(sin_deg(270) == -util::SIN_SCALE);
static_assert(sin_deg(-90) == -util::SIN_SCALE);
static_assert(sin_deg(450) == util::SIN_SCALE);
static_assert(sin_deg(45) > 7071 - 20 && sin_deg(45) < 7071 + 20);
static_assert(util::cos_deg(0) == util::SIN_SCALE);
static_assert(util::wrap(-1, 360) == 359);
static_assert(util::wrap(725, 360) == 5);
static_assert(util::clamp(5, 0, 3) == 3);
static_assert(util::round_to(1234, 10) == 1230);
static_assert(util::round_to(1235, 10) == 1240);
static_assert(util::round_to(-1235, 10) == -1240);
} // namespace fixed_math_tests

// ---- random
// --------------------------------------------------------------------

constexpr bool gaussian_noise_is_bounded_and_centred()
{
    util::xorshift32 rng{12345};
    int64_t sum = 0;
    for(int i = 0; i < 1000; ++i)
    {
        const int32_t n = rng.gaussian(100);
        if(n < -400 || n > 400) // Irwin-Hall(4) is bounded at ~3.5 sigma
            return false;
        sum += n;
    }
    return sum / 1000 > -15 && sum / 1000 < 15; // mean close to 0
}

static_assert(gaussian_noise_is_bounded_and_centred());

// ---- span and network byte helpers
// ---------------------------------------------

constexpr bool span_subspan_clips()
{
    int data[5] = {1, 2, 3, 4, 5};
    util::span<int> s{data};
    return s.size() == 5 && s.subspan(3).size() == 2 && s.subspan(9).empty() &&
           s.subspan(1, 2)[1] == 3 && s.first(10).size() == 5;
}

static_assert(span_subspan_clips());

constexpr bool big_endian_round_trip()
{
    uint8_t b[4] = {};
    net::store_be32(b, 0x0a00'020f);
    return b[0] == 10 && b[3] == 15 && net::load_be32(b) == 0x0a00'020f &&
           net::load_be16(b + 2) == 0x020f;
}

static_assert(big_endian_round_trip());

constexpr bool rfc1071_checksum_example()
{
    // Example from RFC 1071 section 3: sum 0xddf2, checksum ~0xddf2.
    const uint8_t data[8] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    return net::internet_checksum(data) == static_cast<uint16_t>(~0xddf2);
}

static_assert(rfc1071_checksum_example());

constexpr bool checksum_over_valid_header_is_zero()
{
    // A real IPv4 header (from RFC 1071-style examples) with its checksum.
    const uint8_t header[20] =
        {0x45,
         0x00,
         0x00,
         0x73,
         0x00,
         0x00,
         0x40,
         0x00,
         0x40,
         0x11,
         0xb8,
         0x61,
         0xc0,
         0xa8,
         0x00,
         0x01,
         0xc0,
         0xa8,
         0x00,
         0xc7};
    return net::internet_checksum(header) == 0;
}

static_assert(checksum_over_valid_header_is_zero());

// ---- standard atmosphere (cloud levels)
// ------------------------------------------

namespace atmosphere_tests
{
using util::standard_pressure_hpa;
static_assert(standard_pressure_hpa(0) == 1013);
static_assert(standard_pressure_hpa(-100) == 1013); // clamped
static_assert(standard_pressure_hpa(1500) == 846);  // ICAO: 845.6
static_assert(standard_pressure_hpa(3000) == 701);  // ICAO: 701.1
static_assert(standard_pressure_hpa(5500) == 505); // ICAO: 505.0 (interpolated)
static_assert(standard_pressure_hpa(12000) == 194); // ICAO: 193.9
static_assert(standard_pressure_hpa(20000) == 194); // clamped
static_assert(standard_pressure_hpa(1000) > standard_pressure_hpa(1100));
} // namespace atmosphere_tests

} // namespace
