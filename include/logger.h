#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdbool.h>

#include <kywc/log.h>

void logger_init(enum kywc_log_level level, bool log_to_file);

void logger_set_level(enum kywc_log_level level);

void logger_finish(void);

#endif /* _LOGGER_H_ */
