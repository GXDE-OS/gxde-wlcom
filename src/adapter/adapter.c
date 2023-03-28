#include <stdbool.h>

#include "adapter.h"
#include "server.h"
// #ifdef HAVE_WLROOTS_ADAPTER
#include "wlroots/wlroots.h"
// #endif

// XXX: maybe we need an adapter base struct

bool adapter_init(struct server *server)
{
    if (!wlroots_adapter_init(server)) {
        return false;
    }

    if (server->impl && server->impl->init && server->impl->init(server)) {
        return true;
    }

    adapter_finish(server);
    return false;
}

bool adapter_start(struct server *server)
{
    if (server->impl && server->impl->start && server->impl->start(server)) {
        return true;
    }

    adapter_finish(server);
    return false;
}

void adapter_finish(struct server *server)
{
    if (server->impl && server->impl->finish) {
        server->impl->finish(server);
    }
}
