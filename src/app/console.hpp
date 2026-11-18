#pragma once

#include "hal/pl011.hpp"
#include "util/text_stream.hpp"

namespace app
{

// Text output on the UART. Translates '\n' to "\r\n" for terminals.
class console final : public util::text_stream
{
public:
    explicit constexpr console(const hal::pl011& uart) : _uart(uart) {}

private:
    void put(char c) override;

    const hal::pl011& _uart;
};

} // namespace app
