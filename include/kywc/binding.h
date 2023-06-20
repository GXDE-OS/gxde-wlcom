#ifndef _KYWC_BINDING_H_
#define _KYWC_BINDING_H_

#include <stdbool.h>

struct key_binding *kywc_key_binding_create(const char *keybind, const char *desc);

struct key_binding *kywc_key_binding_create_by_symbol(unsigned int keysym, unsigned int modifiers,
                                                      const char *desc);

void kywc_key_binding_destroy(struct key_binding *binding);

bool kywc_key_binding_register(struct key_binding *binding,
                               void (*action)(struct key_binding *binding, void *data), void *data);

bool kywc_key_binding_update(struct key_binding *binding, unsigned int keysym,
                             unsigned int modifiers, const char *desc);

void kywc_key_binding_unregister(struct key_binding *binding);

bool kywc_key_binding_is_registered(struct key_binding *binding);

#endif /* _KYWC_BINDING_H_ */
