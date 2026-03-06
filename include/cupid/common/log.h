#ifndef CUPID_COMMON_LOG_H
#define CUPID_COMMON_LOG_H

#include <stdarg.h>

void cupid_log_info(const char *message);
void cupid_log_infof(const char *format, ...);
void cupid_log_error(const char *message);
void cupid_log_errorf(const char *format, ...);

#endif
