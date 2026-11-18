#include "app/weather_scenario.hpp"

namespace app
{

const weather_scenario* find_scenario(util::span<const uint8_t> name)
{
    for(const weather_scenario& scenario : SCENARIOS)
    {
        size_t i = 0;
        while(i < name.size() && scenario.name[i] != '\0' &&
              static_cast<uint8_t>(scenario.name[i]) == name[i])
            ++i;
        if(i == name.size() && scenario.name[i] == '\0')
            return &scenario;
    }
    return nullptr;
}

} // namespace app
