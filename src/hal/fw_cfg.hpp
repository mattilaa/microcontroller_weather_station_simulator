#pragma once

#include <stddef.h>
#include <stdint.h>

#include "util/span.hpp"

namespace hal
{

// QEMU firmware configuration device (fw_cfg), MMIO interface on virt at
// 0x09020000. It lets the QEMU command line pass named values to the
// firmware, e.g.
//   -fw_cfg name=opt/weather/speed,string=600
class fw_cfg
{
public:
    static constexpr uintptr_t BASE = 0x0902'0000;

    // True if the device is there (its signature reads "QEMU").
    static bool present();

    // Copy the contents of file `name` into `out`; returns its size, or -1
    // if there is no such file. Content beyond out.size() is dropped.
    static int32_t read_file(const char* name, util::span<uint8_t> out);
};

} // namespace hal
