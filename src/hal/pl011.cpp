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

using FR_RXFE = flag<4>; // receive FIFO empty
using FR_TXFF = flag<5>; // transmit FIFO full

using LCRH_FEN = flag<4>;          // FIFO enable
using LCRH_WLEN = bit_field<5, 2>; // word length: 0 = 5 bits ... 3 = 8 bits

using CR_UARTEN = flag<0>;
using CR_TXE = flag<8>;
using CR_RXE = flag<9>;
} // namespace

void pl011::init() const
{
    reg(CR).write(0);    // disable while configuring
    reg(IBRD).write(13); // 115200 baud @ 24 MHz (QEMU ignores this)
    reg(FBRD).write(1);
    reg(LCRH).write(LCRH_WLEN::value<3>() | LCRH_FEN::MASK); // 8N1, FIFOs on
    reg(IMSC).write(0);                                      // polling mode
    reg(CR).write(CR_UARTEN::MASK | CR_TXE::MASK | CR_RXE::MASK);
}

void pl011::put(char c) const
{
    while(reg(FR).test(FR_TXFF::MASK))
    {
    }
    reg(DR).write(static_cast<unsigned char>(c));
}

bool pl011::try_get(char& c) const
{
    if(reg(FR).test(FR_RXFE::MASK))
        return false;
    c = static_cast<char>(reg(DR).read() & 0xFF);
    return true;
}

} // namespace hal
