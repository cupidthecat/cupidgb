#include "cupid/common/log.h"

#include <stdarg.h>
#include <stdio.h>

static void cupid_log_vprint(FILE *stream, const char *level, const char *format, va_list arguments)
{
    fprintf(stream, "[%s] ", level);
    vfprintf(stream, format, arguments);
    fputc('\n', stream);
    fflush(stream);
}

void cupid_log_info(const char *message)
{
    fprintf(stdout, "[INFO] %s\n", message);
    fflush(stdout);
}

void cupid_log_infof(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    cupid_log_vprint(stdout, "INFO", format, arguments);
    va_end(arguments);
}

void cupid_log_error(const char *message)
{
    fprintf(stderr, "[ERROR] %s\n", message);
    fflush(stderr);
}

void cupid_log_errorf(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    cupid_log_vprint(stderr, "ERROR", format, arguments);
    va_end(arguments);
}
