#ifndef _KYWC_LOG_H_
#define _KYWC_LOG_H_

#include <errno.h>
#include <stdarg.h>
#include <string.h>

enum kywc_log_level {
    KYWC_SILENT = 0,
    KYWC_FATAL,
    KYWC_ERROR,
    KYWC_WARN,
    KYWC_INFO,
    KYWC_DEBUG,
    KYWC_LOG_LEVEL_LAST,
};

#ifdef __GNUC__
#define _KYWC_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _KYWC_ATTRIB_PRINTF(start, end)
#endif

void kywc_log(enum kywc_log_level level, const char *format, ...) _KYWC_ATTRIB_PRINTF(2, 3);

enum kywc_log_level kywc_log_get_level(void);

void kywc_vlog(enum kywc_log_level level, const char *format, va_list args)
    _KYWC_ATTRIB_PRINTF(2, 0);

#define kywc_log_errno(level, fmt, ...) kywc_log(level, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif /* _KYWC_LOG_H_ */
