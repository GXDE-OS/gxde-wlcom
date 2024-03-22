// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYWC_IDENTIFIER_H_
#define _KYWC_IDENTIFIER_H_

#define UUID_SIZE 37

#ifdef __GNUC__
#define _KYWC_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _KYWC_ATTRIB_PRINTF(start, end)
#endif

const char *kywc_identifier_generate(const char *format, ...) _KYWC_ATTRIB_PRINTF(1, 2);

const char *kywc_identifier_utf8_generate(const char *format, ...) _KYWC_ATTRIB_PRINTF(1, 2);

/* return 36-byte string (plus trailing '\0') of the form 1b4e28ba-2fa1-11d2-883f-0016d3cca427 */
const char *kywc_identifier_uuid_generate(void);

/* return 32-byte string */
const char *kywc_identifier_md5_generate(void *data, unsigned int len);

/* return 36_byte of md5-uuid */
const char *kywc_identifier_md5_generate_uuid(void *data, unsigned int len);

/* write md5 string to md5_str in size bytes */
void kywc_identifier_md5_generate_ex(void *data, unsigned int len, char *md5_str,
                                     unsigned int size);

#endif /* _KYWC_IDENTIFIER_H_ */
