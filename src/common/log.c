/**
 * @file    log.c
 * @brief   Console logging for informational messages and errors.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Both informational and error messages support plain strings and printf-style
 * formatting.
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "cupid/common/log.h"

#include <stdarg.h>
#include <stdio.h>

/**
 * @brief Internal helper that formats and writes a log message to a stream.
 *
 * Writes a log message in the format "[LEVEL] message\n" to the given stream,
 * then flushes the stream.
 *
 * @param stream    The output stream to write to (e.g. stdout or stderr).
 * @param level     A string representing the log level (e.g. "INFO", "ERROR").
 * @param format    A printf-style format string for the log message.
 * @param arguments A va_list of arguments corresponding to the format string.
 */
static void cupid_log_vprint(FILE *stream, const char *level, const char *format,
                             va_list arguments) {
    fprintf(stream, "[%s] ", level);
    vfprintf(stream, format, arguments);
    fputc('\n', stream);
    fflush(stream);
}

/**
 * @brief Logs a plain info message to stdout.
 *
 * Writes the message in the format "[INFO] message\n" to stdout,
 * then flushes stdout.
 *
 * @param message The message string to log.
 */
void cupid_log_info(const char *message) {
    fprintf(stdout, "[INFO] %s\n", message);
    fflush(stdout);
}

/**
 * @brief Logs a formatted info message to stdout.
 *
 * Writes a formatted message in the format "[INFO] message\n" to stdout,
 * then flushes stdout. Accepts printf-style format arguments.
 *
 * @param format A printf-style format string for the log message.
 * @param ...    Additional arguments corresponding to the format string.
 */
void cupid_log_infof(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    cupid_log_vprint(stdout, "INFO", format, arguments);
    va_end(arguments);
}

/**
 * @brief Logs a plain error message to stderr.
 *
 * Writes the message in the format "[ERROR] message\n" to stderr,
 * then flushes stderr.
 *
 * @param message The message string to log.
 */
void cupid_log_error(const char *message) {
    fprintf(stderr, "[ERROR] %s\n", message);
    fflush(stderr);
}

/**
 * @brief Logs a formatted error message to stderr.
 *
 * Writes a formatted message in the format "[ERROR] message\n" to stderr,
 * then flushes stderr. Accepts printf-style format arguments.
 *
 * @param format A printf-style format string for the log message.
 * @param ...    Additional arguments corresponding to the format string.
 */
void cupid_log_errorf(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    cupid_log_vprint(stderr, "ERROR", format, arguments);
    va_end(arguments);
}
