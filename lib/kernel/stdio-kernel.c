#include "stdio.h"
#include "console.h"
#include "stdio-kernel.h"

void printk(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024] = {0};
    vs_printf(buf, format, args);
    va_end(args);
    console_put_str(buf);
}