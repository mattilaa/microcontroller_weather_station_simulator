#pragma once

#include "app/weather_station.hpp"
#include "net/stack.hpp"

namespace app
{

// UDP text protocol on the weather station's service port. One command per
// datagram, one reply datagram:
//
//   weather          -> {"day":1,"time":"06:10","temperature":12.34,...}
//   time HH:MM       -> "ok time HH:MM"   (jump the simulated clock)
//   log [sequence]   -> one page of logged readings as CSV, from `sequence`
//   advance MINUTES  -> fast-forward the simulation, logging every reading
//   ping             -> "pong"
//   help           -> list of commands
//   anything else  -> "error: unknown command"
class weather_service final : public net::udp_handler
{
public:
    static constexpr uint16_t PORT = 5000;

    explicit constexpr weather_service(weather_station& station)
        : _station(station)
    {
    }

    size_t handle(
        util::span<const uint8_t> request,
        const net::udp_endpoint& from,
        util::span<uint8_t> reply) override;

private:
    weather_station& _station;
};

} // namespace app
