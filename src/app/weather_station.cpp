#include "app/weather_station.hpp"

#include "util/fixed_math.hpp"

namespace app
{

// Unit wrappers are single integers; MLang stores those integers directly.
static_assert(sizeof(weather_station) == 744);
static_assert(sizeof(sensor_model) == 20);
static_assert(sizeof(cloud_layers) == 76);
static_assert(sizeof(weather_reading) == 120);

// Stable pointer/scalar boundary to src/mlang/weather_station.mla.
extern "C" int32_t measure__ptr_struct_Sensor_i32(sensor_model*, int32_t);
extern "C" void station_set_speed__ptr_struct_Station_u32(
    weather_station*,
    uint32_t);
extern "C" void station_update__ptr_struct_Station_u64(
    weather_station*,
    uint64_t);
extern "C" void station_set_scenario__ptr_struct_Station_ptr_struct_Scenario(
    weather_station*,
    const weather_scenario*);
extern "C" void station_fast_forward__ptr_struct_Station_u32(
    weather_station*,
    uint32_t);
extern "C" void station_set_time_of_day__ptr_struct_Station_u32(
    weather_station*,
    uint32_t);
extern "C" void station_simulate__ptr_struct_Station_u64(
    weather_station*,
    uint64_t);
extern "C" void station_sample__ptr_struct_Station_u32(
    weather_station*,
    uint32_t);
extern "C" void station_interval_mean__ptr_struct_Station_ptr_struct_Reading(
    const weather_station*,
    weather_reading*);
extern "C" void station_mean_of_interval__ptr_struct_Station_ptr_struct_Reading(
    const weather_station*,
    weather_reading*);
extern "C" void station_close_record__ptr_struct_Station(weather_station*);
extern "C" void station_reset_interval__ptr_struct_Station(weather_station*);

extern "C" void weather_log_add(
    weather_log* log,
    const weather_reading* reading)
{
    log->add(*reading);
}

extern "C" void write_reading__ptr_u8_ptr_struct_Reading(
    util::text_stream*,
    const weather_reading*);

util::text_stream& operator<<(
    util::text_stream& out,
    const weather_reading& reading)
{
    write_reading__ptr_u8_ptr_struct_Reading(&out, &reading);
    return out;
}

int32_t sensor_model::measure(int32_t true_value)
{
    return measure__ptr_struct_Sensor_i32(this, true_value);
}

// ---- weather_log
// ----------------------------------------------------------------

// The packed record is what keeps the log inside the RAM budget.
static_assert(sizeof(weather_log::entry) == 44);

extern "C" void derive_cloud_levels__ptr_struct_Clouds(cloud_layers*);

void derive_cloud_levels(cloud_layers& clouds)
{
    derive_cloud_levels__ptr_struct_Clouds(&clouds);
}

extern "C"
{
    void pack_entry__ptr_struct_Entry_ptr_struct_Reading_u32(
        weather_log::entry*,
        const weather_reading*,
        uint32_t);
    void unpack_entry__ptr_struct_Entry_ptr_struct_Reading(
        const weather_log::entry*,
        weather_reading*);
    uint64_t log_find__u32_u64_u32(uint32_t, uint64_t, uint32_t);
}

void weather_log::add(const weather_reading& reading)
{
    if(_entries.full())
    {
        entry oldest{};
        _entries.pop(oldest);
    }
    entry packed{};
    pack_entry__ptr_struct_Entry_ptr_struct_Reading_u32(
        &packed,
        &reading,
        _next_sequence++);
    _entries.push(packed);
}

weather_reading weather_log::entry::reading() const
{
    weather_reading result{};
    unpack_entry__ptr_struct_Entry_ptr_struct_Reading(this, &result);
    return result;
}

size_t weather_log::find(uint32_t sequence) const
{
    return log_find__u32_u64_u32(
        _entries.empty() ? 0 : _entries[0].sequence,
        _entries.size(),
        sequence);
}

// ---- weather_station
// ------------------------------------------------------------

void weather_station::set_speed(uint32_t speed)
{
    station_set_speed__ptr_struct_Station_u32(this, speed);
}

void weather_station::update(uint64_t now_ms)
{
    station_update__ptr_struct_Station_u64(this, now_ms);
}

void weather_station::set_scenario(const weather_scenario& scenario)
{
    station_set_scenario__ptr_struct_Station_ptr_struct_Scenario(
        this,
        &scenario);
}

void weather_station::fast_forward(uint32_t minutes)
{
    station_fast_forward__ptr_struct_Station_u32(this, minutes);
}

void weather_station::set_time_of_day(uint32_t minute)
{
    station_set_time_of_day__ptr_struct_Station_u32(this, minute);
}

void weather_station::simulate(uint64_t sim_seconds)
{
    station_simulate__ptr_struct_Station_u64(this, sim_seconds);
}

void weather_station::sample(uint32_t weight_seconds)
{
    station_sample__ptr_struct_Station_u32(this, weight_seconds);
}

weather_reading weather_station::interval_mean() const
{
    weather_reading result{};
    station_interval_mean__ptr_struct_Station_ptr_struct_Reading(this, &result);
    return result;
}

weather_reading weather_station::mean_of_interval() const
{
    weather_reading result{};
    station_mean_of_interval__ptr_struct_Station_ptr_struct_Reading(
        this,
        &result);
    return result;
}

void weather_station::close_record()
{
    station_close_record__ptr_struct_Station(this);
}

void weather_station::reset_interval()
{
    station_reset_interval__ptr_struct_Station(this);
}

} // namespace app
