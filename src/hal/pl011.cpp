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

using FR_RXFE = Flag<4>; // receive FIFO empty
using FR_TXFF = Flag<5>; // transmit FIFO full

using LCRH_FEN = Flag<4>;         // FIFO enable
using LCRH_WLEN = BitField<5, 2>; // word length: 0 = 5 bits ... 3 = 8 bits

using CR_UARTEN = Flag<0>;
using CR_TXE = Flag<8>;
using CR_RXE = Flag<9>;
} // namespace

void Pl011::init() const
{
    reg(CR).write(0);    // disable while configuring
    reg(IBRD).write(13); // 115200 baud @ 24 MHz (QEMU ignores this)
    reg(FBRD).write(1);
    reg(LCRH).write(LCRH_WLEN::value<3>() | LCRH_FEN::mask); // 8N1, FIFOs on
    reg(IMSC).write(0);                                      // polling mode
    reg(CR).write(CR_UARTEN::mask | CR_TXE::mask | CR_RXE::mask);
}

void Pl011::put(char c) const
{
    while(reg(FR).test(FR_TXFF::mask))
    {
    }
    reg(DR).write(static_cast<unsigned char>(c));
}

bool Pl011::try_get(char& c) const
{
    if(reg(FR).test(FR_RXFE::mask))
        return false;
    c = static_cast<char>(reg(DR).read() & 0xFF);
    return true;
}

} // namespace hal
