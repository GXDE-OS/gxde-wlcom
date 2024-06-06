// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>

#include <kywc/identifier.h>

static void strip_whitespace(char *str)
{
    size_t len = strlen(str);
    size_t start = strspn(str, " \f\n\r\t\v");
    memmove(str, &str[start], len + 1 - start);

    if (*str) {
        for (len -= start + 1; isspace(str[len]); --len) {
        }
        str[len + 1] = '\0';
    }
}

static void replace_unprintable(char *str)
{
    char *p = str;
    for (; *p; ++p) {
        if (*p == ' ' || !isprint(*p)) {
            *p = '_';
        }
    }
}

const char *kywc_identifier_utf8_generate(const char *format, ...)
{
    if (!format) {
        return NULL;
    }

    int len = 0;
    char *identifier = NULL;
    va_list args;

    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args) + 1;
    va_end(args);

    identifier = malloc(len);
    if (!identifier) {
        return NULL;
    }

    va_start(args, format);
    vsnprintf(identifier, len, format, args);
    va_end(args);

    return identifier;
}

const char *kywc_identifier_generate(const char *format, ...)
{
    if (!format) {
        return NULL;
    }

    int len = 0;
    char *identifier = NULL;
    va_list args;

    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args) + 1;
    va_end(args);

    identifier = malloc(len);
    if (!identifier) {
        return NULL;
    }

    va_start(args, format);
    vsnprintf(identifier, len, format, args);
    va_end(args);

    strip_whitespace(identifier);
    replace_unprintable(identifier);

    return identifier;
}

const char *kywc_identifier_time_generate(const char *prefix, const char *suffix)
{
    size_t len = (prefix ? strlen(prefix) : 0) + (suffix ? strlen(suffix) : 0) + 20;
    char *identifier = malloc(len);
    if (!identifier) {
        return NULL;
    }

    time_t timer = time(NULL);
    struct tm *tm = localtime(&timer);
    snprintf(identifier, len, "%s%04d-%02d-%02d_%02d-%02d-%02d%s", prefix ? prefix : "",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
             suffix ? suffix : "");

    return identifier;
}

const char *kywc_identifier_rand_generate(char *data, int suffixlen)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    long r = ts.tv_nsec;
    char *buf = data + strlen(data) - 6 - suffixlen;

    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }

    return data;
}

static char hexchar(int x)
{
    static const char table[16] = "0123456789abcdef";
    return table[x & 15];
}

const char *kywc_identifier_uuid_generate(void)
{
    unsigned char uuid[16];
    RAND_bytes(uuid, sizeof(uuid));

    /* Set UUID version to 4 --- truly uuid generation */
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    /* Set the UUID variant to DCE */
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    char *uuid_str = malloc(37);
    size_t k = 0;
    for (size_t n = 0; n < 16; n++) {
        if (n == 4 || n == 6 || n == 8 || n == 10) {
            uuid_str[k++] = '-';
        }
        uuid_str[k++] = hexchar(uuid[n] >> 4);
        uuid_str[k++] = hexchar(uuid[n] & 0xF);
    }
    uuid_str[k] = 0;

    return uuid_str;
}

const char *kywc_identifier_md5_generate(void *data, unsigned int len)
{
    unsigned char md5[16] = { 0 };
    EVP_Digest(data, len, md5, NULL, EVP_md5(), NULL);

    char *md5_str = malloc(UUID_SIZE);
    size_t k = 0;
    for (size_t n = 0; n < 16; n++) {
        md5_str[k++] = hexchar(md5[n] >> 4);
        md5_str[k++] = hexchar(md5[n] & 0xF);
    }
    md5_str[k] = 0;

    return md5_str;
}

void kywc_identifier_md5_generate_ex(void *data, unsigned int len, char *md5_str,
                                     unsigned int str_size)
{
    unsigned char md5[16] = { 0 };
    EVP_Digest(data, len, md5, NULL, EVP_md5(), NULL);

    size_t k = 0;
    for (size_t n = 0; n < str_size / 2; n++) {
        md5_str[k++] = hexchar(md5[n] >> 4);
        md5_str[k++] = hexchar(md5[n] & 0xF);
    }
    md5_str[str_size - 1] = 0;
}

const char *kywc_identifier_md5_generate_uuid(void *data, unsigned int len)
{
    unsigned char md5[16] = { 0 };
    EVP_Digest(data, len, md5, NULL, EVP_md5(), NULL);

    char *uuid_str = malloc(UUID_SIZE);
    size_t k = 0;
    for (size_t n = 0; n < 16; n++) {
        if (n == 4 || n == 6 || n == 8 || n == 10) {
            uuid_str[k++] = '-';
        }
        uuid_str[k++] = hexchar(md5[n] >> 4);
        uuid_str[k++] = hexchar(md5[n] & 0xF);
    }

    uuid_str[k] = 0;
    return uuid_str;
}

const char *kywc_identifier_base64_generate(const void *data, unsigned int len)
{
    /* every 3 bytes to 4 bytes and 1 NUL terminator */
    size_t size = (len + 2) / 3 * 4 + 1;
    char *base64 = malloc(size);
    if (!base64) {
        return NULL;
    }

    EVP_EncodeBlock((unsigned char *)base64, data, len);
    return base64;
}
