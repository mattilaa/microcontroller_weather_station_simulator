// C++ interface to the mlang implementation in src/mlang/weather_model.mla.
#include "app/weather_model.hpp"

namespace app
{

// Keep these in sync with verify_layout() in weather_station.mla.
static_assert(sizeof(weather_model) == 232);
static_assert(sizeof(weather_scenario) == 104);
static_assert(sizeof(weather_conditions) == 108);

extern "C" int32_t daily_temperature__ptr_struct_Model_u32(
    const weather_model*,
    uint32_t);
extern "C" int32_t daily_convection__u32(uint32_t);
extern "C" int32_t lcl_hpa__i32_i32(int32_t, int32_t);
extern "C" int32_t stratus_top__i32_i32(int32_t, int32_t);
extern "C" int32_t stratocumulus_top__i32_i32(int32_t, int32_t);
extern "C" void altocumulus_into__ptr_struct_Levels_i32_i32_i32(
    weather_model::levels*,
    int32_t,
    int32_t,
    int32_t);
extern "C" int32_t daily_mean_wind__ptr_struct_Model_u32(
    const weather_model*,
    uint32_t);
extern "C" int32_t cloudiness__ptr_struct_Model(const weather_model*);
extern "C" void set_scenario__ptr_struct_Model_ptr_struct_Scenario(
    weather_model*,
    const weather_scenario*);
extern "C" uint32_t day__ptr_struct_Model(const weather_model*);
extern "C" uint32_t second_of_day__ptr_struct_Model(const weather_model*);
extern "C" uint32_t minute_of_day__ptr_struct_Model(const weather_model*);
extern "C" void advance_simulated__ptr_struct_Model_u32(
    weather_model*,
    uint32_t);
extern "C" int32_t take_rain__ptr_struct_Model(weather_model*);
extern "C" void set_time_of_day__ptr_struct_Model_u32(weather_model*, uint32_t);
extern "C" void step__ptr_struct_Model_u32(weather_model*, uint32_t);
extern "C" void update_conditions__ptr_struct_Model(weather_model*);

int32_t weather_model::daily_temperature(uint32_t minute_of_day) const
{
    return daily_temperature__ptr_struct_Model_u32(this, minute_of_day);
}

int32_t weather_model::daily_convection(uint32_t minute_of_day)
{
    return daily_convection__u32(minute_of_day);
}

int32_t weather_model::lcl_hpa(int32_t humidity, int32_t surface_pressure)
{
    return lcl_hpa__i32_i32(humidity, surface_pressure);
}

int32_t weather_model::stratus_top(int32_t lcl, int32_t amount)
{
    return stratus_top__i32_i32(lcl, amount);
}

int32_t weather_model::stratocumulus_top(int32_t lcl, int32_t amount)
{
    return stratocumulus_top__i32_i32(lcl, amount);
}

weather_model::levels weather_model::altocumulus_levels(
    int32_t altostratus_base,
    int32_t altostratus_top,
    int32_t amount)
{
    levels result{};
    altocumulus_into__ptr_struct_Levels_i32_i32_i32(
        &result,
        altostratus_base,
        altostratus_top,
        amount);
    return result;
}

int32_t weather_model::daily_mean_wind(uint32_t minute_of_day) const
{
    return daily_mean_wind__ptr_struct_Model_u32(this, minute_of_day);
}

int32_t weather_model::cloudiness() const
{
    return cloudiness__ptr_struct_Model(this);
}

void weather_model::set_scenario(const weather_scenario& scenario)
{
    set_scenario__ptr_struct_Model_ptr_struct_Scenario(this, &scenario);
}

uint32_t weather_model::day() const
{
    return day__ptr_struct_Model(this);
}

uint32_t weather_model::second_of_day() const
{
    return second_of_day__ptr_struct_Model(this);
}

uint32_t weather_model::minute_of_day() const
{
    return minute_of_day__ptr_struct_Model(this);
}

void weather_model::advance_simulated(uint32_t sim_seconds)
{
    advance_simulated__ptr_struct_Model_u32(this, sim_seconds);
}

int32_t weather_model::take_rain()
{
    return take_rain__ptr_struct_Model(this);
}

void weather_model::set_time_of_day(uint32_t minute)
{
    set_time_of_day__ptr_struct_Model_u32(this, minute);
}

void weather_model::step(uint32_t dt)
{
    step__ptr_struct_Model_u32(this, dt);
}

void weather_model::update_conditions()
{
    update_conditions__ptr_struct_Model(this);
}

} // namespace app
