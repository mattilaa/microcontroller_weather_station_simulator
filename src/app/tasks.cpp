// Hardware task adapters. Application dispatch and reporting are in MLang.
#include "app/tasks.hpp"
#include "app/mlang_bridge.hpp"

extern "C"
{
    void weather_task_run__ptr_struct_Station_ptr_u8_bool_u64(
        app::weather_station*,
        util::text_stream*,
        bool,
        uint64_t);
    void print_help__ptr_u8(util::text_stream*);
    void console_command__ptr_struct_Station_ptr_u8_ptr_bool_ptr_u8_u64_u32(
        app::weather_station*,
        util::text_stream*,
        bool*,
        const app::mlang_console_context*,
        uint64_t,
        uint32_t);
    void app_print_network(const app::mlang_console_context*);
}

namespace app
{
void weather_task::run(uint64_t now_ms)
{
    weather_task_run__ptr_struct_Station_ptr_u8_bool_u64(
        &_station,
        &_out,
        _settings.verbose,
        now_ms);
}

void network_task::run(uint64_t)
{
    if(_nic.ready())
        _stack.poll();
}

void console_task::print_help()
{
    print_help__ptr_u8(&_out);
}

void console_task::print_network()
{
    const mlang_console_context context{_out, _nic, _stack, _scheduler};
    app_print_network(&context);
}

void console_task::run(uint64_t now_ms)
{
    const mlang_console_context context{_out, _nic, _stack, _scheduler};
    char key;
    while(_uart.try_get(key))
        console_command__ptr_struct_Station_ptr_u8_ptr_bool_ptr_u8_u64_u32(
            &_station,
            &_out,
            &_settings.verbose,
            &context,
            now_ms,
            static_cast<uint8_t>(key));
}
} // namespace app
