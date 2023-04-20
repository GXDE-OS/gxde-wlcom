#ifndef _KYWC_BINDING_H_
#define _KYWC_BINDING_H_

#include <stdbool.h>

struct key_binding *kywc_key_binding_create(const char *keybind, const char *desc);

void kywc_key_binding_destroy(struct key_binding *binding);

bool kywc_key_binding_register(struct key_binding *binding,
                               void (*action)(struct key_binding *binding, void *data), void *data);

#endif /* _KYWC_BINDING_H_ */
