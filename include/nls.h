// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _NLS_H_
#define _NLS_H_

#if HAVE_NLS
#include <libintl.h>
#define tr gettext
#else
#define tr(s) (s)
#endif

#endif /* _NLS_H_ */
