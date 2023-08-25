#ifndef _NLS_H_
#define _NLS_H_

#if HAVE_NLS
#include <libintl.h>
#include <locale.h>
#define tr gettext
#else
#define tr(s) (s)
#endif

#endif
