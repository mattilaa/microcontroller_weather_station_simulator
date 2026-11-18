// Borrowed byte I/O, static storage and hardware access for MLang.
#include "app/mlang_bridge.hpp"
#include "app/weather_station.hpp"
#include "hal/semihosting.hpp"
#include "runtime/stack_usage.hpp"

extern "C"
{
    void app_put(util::text_stream* out, uint32_t byte)
    {
        *out << static_cast<char>(byte);
    }

    const app::weather_scenario* app_scenario_at(uint32_t index)
    {
        return &app::SCENARIOS[index];
    }

    uint32_t app_scenario_count()
    {
        return static_cast<uint32_t>(app::SCENARIO_COUNT);
    }

    uint64_t app_log_size(const app::weather_log* log)
    {
        return log->size();
    }

    uint32_t app_log_next(const app::weather_log* log)
    {
        return log->next_sequence();
    }

    uint64_t app_log_find(const app::weather_log* log, uint32_t sequence)
    {
        return log->find(sequence);
    }

    uint32_t app_log_read(
        const app::weather_log* log,
        uint64_t index,
        app::weather_reading* reading)
    {
        const auto& entry = (*log)[index];
        *reading = entry.reading();
        return entry.sequence;
    }

    uint64_t app_stack_peak()
    {
        return runtime::stack_peak();
    }

    uint64_t app_stack_size()
    {
        return runtime::stack_size();
    }

    void app_quit()
    {
        hal::semihosting::exit(0);
    }

    void app_print_network(const app::mlang_console_context* context)
    {
        auto& out = context->out;
        if(!context->nic.ready())
        {
            out << "network: " << hal::to_string(context->nic.state()) << '\n';
            return;
        }
        const net::stack::counters& stats = context->stack.stats();
        out << "network: up, MAC " << context->nic.mac() << ", IP "
            << context->stack.config().address << ", UDP port "
            << context->stack.udp_port() << '\n'
            << "  frames rx " << stats.frames_received << ", tx "
            << stats.frames_sent << ", dropped " << stats.dropped << '\n'
            << "  arp replies " << stats.arp_replies << ", echo replies "
            << stats.echo_replies << ", udp requests " << stats.udp_requests
            << '\n';
    }

    void app_print_tasks(const app::mlang_console_context* context)
    {
        for(const app::task* task : context->scheduler)
            context->out << "  task " << task->name() << ": period "
                         << task->period_ms() << " ms, runs " << task->runs()
                         << '\n';
    }
}
