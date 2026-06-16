// =========================================================================
// lib/core/logger.c — Error logging for diagnostics
// =========================================================================
#include "src/types.h"
#include <stdio.h>
#include <stdarg.h>

void log_error(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    vswprintf(buf, 1024, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}
