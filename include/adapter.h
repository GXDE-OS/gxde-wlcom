#ifndef _ADAPTER_H_
#define _ADAPTER_H_

#include <stdbool.h>

struct server;

bool adapter_init(struct server *server);

bool adapter_start(struct server *server);

void adapter_finish(struct server *server);

#endif /* _ADAPTER_H_ */
