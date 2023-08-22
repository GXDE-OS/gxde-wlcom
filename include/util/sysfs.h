#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <stdbool.h>
#include <stdint.h>

bool sysfs_read_uint64(const char *filename, uint64_t *val);

bool sysfs_write_uint64(const char *filename, uint64_t val);

#endif /* _SYSFS_H_ */
