#pragma once

#include "app/scheduler.hpp"
#include "hal/virtio_net.hpp"
#include "net/stack.hpp"
#include "util/text_stream.hpp"

namespace app
{
// Borrowed hardware/report context, valid only during a console call.
struct mlang_console_context
{
    util::text_stream& out;
    const hal::virtio_net& nic;
    const net::stack& stack;
    const scheduler& scheduler;
};
} // namespace app
