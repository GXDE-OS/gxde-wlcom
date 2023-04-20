#ifndef _KYWC_IDENTIFIER_H_
#define _KYWC_IDENTIFIER_H_

#ifdef __GNUC__
#define _KYWC_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _KYWC_ATTRIB_PRINTF(start, end)
#endif

const char *kywc_identifier_generate(const char *format, ...) _KYWC_ATTRIB_PRINTF(1, 2);

#endif /* _KYWC_IDENTIFIER_H_ */
