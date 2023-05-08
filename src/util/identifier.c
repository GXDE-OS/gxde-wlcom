#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
