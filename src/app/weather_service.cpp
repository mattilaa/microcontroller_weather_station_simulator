// UDP transport adapter; parsing and application behavior live in MLang.
#include "app/weather_service.hpp"

extern "C" void handle_command__ptr_struct_Station_ptr_u8_u64_ptr_u8(
    app::weather_station*,
    const uint8_t*,
    uint64_t,
    util::text_stream*);

namespace app
{
size_t weather_service::handle(
    util::span<const uint8_t> request,
    const net::udp_endpoint&,
    util::span<uint8_t> reply)
{
    util::buffer_stream
        out{reinterpret_cast<char*>(reply.data()), reply.size()};
    handle_command__ptr_struct_Station_ptr_u8_u64_ptr_u8(
        &_station,
        request.data(),
        request.size(),
        &out);
    return out.size();
}
} // namespace app
