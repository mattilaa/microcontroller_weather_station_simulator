#pragma once

namespace hal::semihosting
{

// Ask QEMU to exit (requires `qemu ... -semihosting`). Halts otherwise.
[[noreturn]] void exit(int code);

} // namespace hal::semihosting
