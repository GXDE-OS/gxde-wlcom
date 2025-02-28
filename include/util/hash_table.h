// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_HASH_TABLE_H_
#define _UTIL_HASH_TABLE_H_

#include <stdbool.h>
#include <stdint.h>

struct hash_entry {
    uint32_t hash;
    const void *key;
    void *data;
};

typedef uint32_t (*hash_func_t)(const void *key);
typedef bool (*equal_func_t)(const void *a, const void *b);

struct hash_table *hash_table_create(hash_func_t hash, equal_func_t equal);

struct hash_table *hash_table_create_pointer(void);

struct hash_table *hash_table_create_string(void);

struct hash_table *hash_table_create_int(void);

bool hash_table_set_max_entries(struct hash_table *ht, uint32_t max_entries);

uint32_t hash_table_get_entries(const struct hash_table *ht);

void hash_table_destroy(struct hash_table *ht);

/**
 * hash_entry may be invalid after insert or set_max_entries.
 */
struct hash_entry *hash_table_insert(struct hash_table *ht, const void *key, void *data);

struct hash_entry *hash_table_insert_hash(struct hash_table *ht, uint32_t hash, const void *key,
                                          void *data);

struct hash_entry *hash_table_search(const struct hash_table *ht, const void *key);

struct hash_entry *hash_table_search_hash(const struct hash_table *ht, uint32_t hash,
                                          const void *key);

struct hash_entry *hash_table_next_entry(struct hash_table *ht, struct hash_entry *entry);

void hash_table_remove(struct hash_table *ht, struct hash_entry *entry);

void hash_table_remove_key(struct hash_table *ht, const void *key);

void hash_table_remove_hash(struct hash_table *ht, uint32_t hash, const void *key);

#define hash_table_for_each(entry, ht)                                                             \
    for (entry = hash_table_next_entry(ht, NULL); entry != NULL;                                   \
         entry = hash_table_next_entry(ht, entry))

#endif /* _UTIL_STRING_H_ */
