#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#include <stdbool.h>

struct server;

#if HAVE_XWAYLAND
bool xwayland_server_create(struct server *server);
void xwayland_server_destroy(void);
#else
static __attribute__((unused)) inline bool xwayland_server_create(struct server *server)
{
    return false;
}
static __attribute__((unused)) inline void xwayland_server_destroy(void) {}
#endif

#endif /* _XWAYLAND_H_ */
