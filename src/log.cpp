#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

void logf(const char *fmt, ...)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%5ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    putchar('\n');
    fflush(stdout);
}
