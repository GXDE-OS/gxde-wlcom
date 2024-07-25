// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _NLS_H_
#define _NLS_H_

#if HAVE_NLS
#include <libintl.h>
#define tr gettext
#else
#define tr(s) (s)
#endif

#include <locale.h>
#include <stdbool.h>
#include <string.h>

static __attribute__((unused)) inline bool nls_layout_is_right_to_left(void)
{
    const char *locale = setlocale(LC_ALL, NULL);
    return locale &&
           (strstr(locale, "ug_CN") || strstr(locale, "kk_KZ") || strstr(locale, "ky_KG"));
}

static __attribute__((unused)) inline bool nls_text_is_right_align(void)
{
    const char *locale = setlocale(LC_ALL, NULL);
    return locale && (strstr(locale, "kk_KZ") || strstr(locale, "ky_KG"));
}

#endif /* _NLS_H_ */
