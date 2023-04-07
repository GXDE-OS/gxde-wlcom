#ifndef _MD5_H_
#define _MD5_H_

#include <stdint.h>
#include <stdio.h>

#define MD5_DIGEST_LENGTH 16

void md5_generate(const void *data, size_t length, uint8_t *hash);

char *md5_to_string(uint8_t *md5, size_t len, char *buf, size_t buf_size);

#endif /* _MD5H_ */
