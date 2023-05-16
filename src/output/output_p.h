#ifndef _OUTPUT_P_H_
#define _OUTPUT_P_H_

#include <kywc/log.h>

#include "output.h"
#include "server.h"

#if HAVE_KDE_OUTPUT
bool kde_output_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool kde_output_management_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_WLR_OUTPUT
bool wlr_output_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool wlr_output_management_create(struct server *server)
{
    return false;
}
#endif

#if 1 // HAVE_LAYOUT
bool layout_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool layout_manager_create(struct server *server)
{
    return false;
}
#endif

#endif /* _OUTPUT_P_H_ */
