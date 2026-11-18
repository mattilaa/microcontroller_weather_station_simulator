#include "app/console.hpp"

namespace app
{

void console::put(char c)
{
    if(c == '\n')
        _uart.put('\r');
    _uart.put(c);
}

} // namespace app
