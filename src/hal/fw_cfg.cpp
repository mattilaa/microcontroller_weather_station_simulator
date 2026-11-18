#include "hal/fw_cfg.hpp"

namespace hal
{

namespace
{
// Registers (QEMU docs/specs/fw_cfg.rst): the data register streams the
// selected item byte by byte; the selector is a big-endian 16-bit register.
constexpr uintptr_t DATA = fw_cfg::BASE + 0x0;
constexpr uintptr_t SELECTOR = fw_cfg::BASE + 0x8;

constexpr uint16_t KEY_SIGNATURE = 0x0000;
constexpr uint16_t KEY_FILE_DIR = 0x0019;
constexpr size_t FILE_NAME_SIZE = 56;

void select(uint16_t key)
{
    // The device is big-endian; our stores are little-endian.
    *reinterpret_cast<volatile uint16_t*>(SELECTOR) =
        static_cast<uint16_t>((key >> 8) | (key << 8));
}

uint8_t read_byte()
{
    return *reinterpret_cast<volatile uint8_t*>(DATA);
}

uint32_t read_be32()
{
    uint32_t v = 0;
    for(int i = 0; i < 4; ++i)
        v = (v << 8) | read_byte();
    return v;
}

uint16_t read_be16()
{
    const uint16_t high = read_byte();
    return static_cast<uint16_t>((high << 8) | read_byte());
}
} // namespace

bool fw_cfg::present()
{
    select(KEY_SIGNATURE);
    return read_byte() == 'Q' && read_byte() == 'E' && read_byte() == 'M' &&
           read_byte() == 'U';
}

int32_t fw_cfg::read_file(const char* name, util::span<uint8_t> out)
{
    if(!present())
        return -1;

    // The file directory: a count, then 64-byte entries
    // { be32 size; be16 select; u16 reserved; char name[56]; }.
    select(KEY_FILE_DIR);
    const uint32_t count = read_be32();
    for(uint32_t entry = 0; entry < count; ++entry)
    {
        const uint32_t size = read_be32();
        const uint16_t key = read_be16();
        read_be16(); // reserved

        bool match = true;
        bool name_ended = false;
        for(size_t i = 0; i < FILE_NAME_SIZE; ++i)
        {
            const char c = static_cast<char>(read_byte());
            if(name_ended)
                continue;
            if(c != name[i])
                match = false;
            if(c == '\0' || name[i] == '\0')
                name_ended = true;
        }
        if(!match)
            continue;

        select(key);
        for(uint32_t i = 0; i < size && i < out.size(); ++i)
            out[i] = read_byte();
        return static_cast<int32_t>(size);
    }
    return -1;
}

} // namespace hal
