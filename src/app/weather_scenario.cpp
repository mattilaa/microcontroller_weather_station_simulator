#include "app/weather_scenario.hpp"

extern "C" int32_t find_scenario_index__ptr_u8_u64_u64(
    const uint8_t*,
    uint64_t,
    uint64_t);

namespace app
{
const weather_scenario* find_scenario(util::span<const uint8_t> name)
{
    const int32_t index =
        find_scenario_index__ptr_u8_u64_u64(name.data(), name.size(), 0);
    return index < 0 ? nullptr : &SCENARIOS[index];
}
} // namespace app
