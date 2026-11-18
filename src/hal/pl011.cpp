#include "hal/pl011.hpp"

namespace hal
{

namespace
{
constexpr uintptr_t DR = 0x00;
constexpr uintptr_t FR = 0x18;
constexpr uintptr_t IBRD = 0x24;
constexpr uintptr_t FBRD = 0x28;
constexpr uintptr_t LCRH = 0x2C;
constexpr uintptr_t CR = 0x30;
constexpr uintptr_t IMSC = 0x38;

constexpr uint32_t FR_RXFE = 1u << 4;
constexpr uint32_t FR_TXFF = 1u << 5;

constexpr uint32_t LCRH_FEN = 1u << 4;
constexpr uint32_t LCRH_WLEN_8 = 3u << 5;

constexpr uint32_t CR_UARTEN = 1u << 0;
constexpr uint32_t CR_TXE = 1u << 8;
constexpr uint32_t CR_RXE = 1u << 9;
} // namespace

void Pl011::init() const
{
    reg(CR).write(0);    // disable while configuring
    reg(IBRD).write(13); // 115200 baud @ 24 MHz (QEMU ignores this)
    reg(FBRD).write(1);
    reg(LCRH).write(LCRH_WLEN_8 | LCRH_FEN); // 8N1, FIFOs on
    reg(IMSC).write(0);                      // polling mode
    reg(CR).write(CR_UARTEN | CR_TXE | CR_RXE);
}

void Pl011::put(char c) const
{
    while(reg(FR).test(FR_TXFF))
    {
    }
    reg(DR).write(static_cast<unsigned char>(c));
}

bool Pl011::try_get(char& c) const
{
    if(reg(FR).test(FR_RXFE))
        return false;
    c = static_cast<char>(reg(DR).read() & 0xFF);
    return true;
}

} // namespace hal
