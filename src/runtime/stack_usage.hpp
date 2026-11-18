#pragma once

#include <stddef.h>

namespace runtime
{

// Size of the stack region reserved by the linker script.
size_t stack_size();

// Peak stack use since boot (high-water mark). startup.S fills the stack
// with a pattern; this finds the deepest word that has been overwritten.
size_t stack_peak();

} // namespace runtime
