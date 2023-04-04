#ifndef _OUTPUT_P_H_
#define _OUTPUT_P_H_

#include <kywc/log.h>

#include "output.h"

#if 1 // HAVE_KDE_OUTPUT
bool kde_output_management_create(struct wl_display *display);
#else
static __attribute__((unused)) inline bool kde_output_management_create(struct wl_display *display)
{
    return false;
}
#endif

#endif /* _OUTPUT_P_H_ */
