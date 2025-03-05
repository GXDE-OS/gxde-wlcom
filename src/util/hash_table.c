// SPDX-FileCopyrightText: 2009,2012 Intel Corporation
// SPDX-FileCopyrightText: 1988-2004 Keith Packard and Bart Massey.
// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

/**
 * hash table adapted from mesa hash_table.c
 *
 * Copyright © 2009,2012 Intel Corporation
 * Copyright © 1988-2004 Keith Packard and Bart Massey.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Keith Packard <keithp@keithp.com>
 */

#include <stdlib.h>
#include <string.h>

#include "util/hash_table.h"

#define DELETED_KEY_VALUE (void *)(uintptr_t)(-1)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct hash_size {
    uint32_t max_entries;
    uint32_t size, rehash;
    uint64_t size_magic, rehash_magic;
};

struct hash_table {
    struct hash_entry *table;

    hash_func_t hash;
    equal_func_t equal;
    void *data;

    uint32_t size_index;
    struct hash_size hash_size;

    uint32_t entries;
    uint32_t deleted_entries;
};

static inline uint32_t fast_urem32(uint32_t n, uint32_t d, uint64_t magic)
{
    uint64_t lowbits = magic * n;
#ifdef __SIZEOF_INT128__
    return ((__uint128_t)lowbits * d) >> 64;
#else
    uint32_t b0 = (uint32_t)lowbits;
    uint32_t b1 = lowbits >> 32;
    return ((((uint64_t)d * b0) >> 32) + (uint64_t)d * b1) >> 32;
#endif
}

/**
 * From Knuth -- a good choice for hash/rehash values is p, p-2 where
 * p and p-2 are both prime.  These tables are sized to have an extra 10%
 * free to avoid exponential performance degradation as the hash table fills
 */
static const struct hash_size hash_sizes[] = {
#define REMAINDER_MAGIC(divisor) ((uint64_t)~0ull / (divisor) + 1)
#define ENTRY(max_entries, size, rehash)                                                           \
    { max_entries, size, rehash, REMAINDER_MAGIC(size), REMAINDER_MAGIC(rehash) }
    ENTRY(2, 5, 3),
    ENTRY(4, 7, 5),
    ENTRY(8, 13, 11),
    ENTRY(16, 19, 17),
    ENTRY(32, 43, 41),
    ENTRY(64, 73, 71),
    ENTRY(128, 151, 149),
    ENTRY(256, 283, 281),
    ENTRY(512, 571, 569),
    ENTRY(1024, 1153, 1151),
    ENTRY(2048, 2269, 2267),
    ENTRY(4096, 4519, 4517),
    ENTRY(8192, 9013, 9011),
    ENTRY(16384, 18043, 18041),
    ENTRY(32768, 36109, 36107),
    ENTRY(65536, 72091, 72089),
    ENTRY(131072, 144409, 144407),
    ENTRY(262144, 288361, 288359),
    ENTRY(524288, 576883, 576881),
    ENTRY(1048576, 1153459, 1153457),
    ENTRY(2097152, 2307163, 2307161),
    ENTRY(4194304, 4613893, 4613891),
    ENTRY(8388608, 9227641, 9227639),
    ENTRY(16777216, 18455029, 18455027),
    ENTRY(33554432, 36911011, 36911009),
    ENTRY(67108864, 73819861, 73819859),
    ENTRY(134217728, 147639589, 147639587),
    ENTRY(268435456, 295279081, 295279079),
    ENTRY(536870912, 590559793, 590559791),
    ENTRY(1073741824, 1181116273, 1181116271),
    ENTRY(2147483648ul, 2362232233ul, 2362232231ul)
};

static int entry_is_free(const struct hash_entry *entry)
{
    return entry->key == NULL;
}

static int entry_is_deleted(const struct hash_table *ht, struct hash_entry *entry)
{
    return entry->key == DELETED_KEY_VALUE;
}

static int entry_is_present(const struct hash_table *ht, struct hash_entry *entry)
{
    return entry->key != NULL && entry->key != DELETED_KEY_VALUE;
}

static void hash_table_insert_rehash(struct hash_table *ht, uint32_t hash, const void *key,
                                     void *data)
{
    uint32_t size = ht->hash_size.size;
    uint32_t start_hash_address = fast_urem32(hash, size, ht->hash_size.size_magic);
    uint32_t double_hash = 1 + fast_urem32(hash, ht->hash_size.rehash, ht->hash_size.rehash_magic);
    uint32_t hash_address = start_hash_address;

    do {
        struct hash_entry *entry = ht->table + hash_address;
        if (entry->key == NULL) {
            entry->hash = hash;
            entry->key = key;
            entry->data = data;
            return;
        }
        hash_address += double_hash;
        if (hash_address >= size) {
            hash_address -= size;
        }
    } while (true);
}

static void hash_table_rehash(struct hash_table *ht, uint32_t new_size_index)
{
    if (ht->size_index == new_size_index && ht->deleted_entries == ht->hash_size.max_entries) {
        memset(ht->table, 0, sizeof(struct hash_entry) * hash_sizes[ht->size_index].size);
        ht->entries = ht->deleted_entries = 0;
        return;
    }

    if (new_size_index >= ARRAY_SIZE(hash_sizes)) {
        return;
    }

    struct hash_entry *table = calloc(hash_sizes[new_size_index].size, sizeof(struct hash_entry));
    if (!table) {
        return;
    }

    struct hash_table old_ht = *ht;

    ht->table = table;
    ht->size_index = new_size_index;
    ht->hash_size = hash_sizes[ht->size_index];
    ht->entries = ht->deleted_entries = 0;

    struct hash_entry *entry;
    hash_table_for_each(entry, &old_ht) {
        hash_table_insert_rehash(ht, entry->hash, entry->key, entry->data);
    }

    ht->entries = old_ht.entries;
    free(old_ht.table);
}

struct hash_table *hash_table_create(hash_func_t hash, equal_func_t equal, void *data)
{
    struct hash_table *ht = calloc(1, sizeof(*ht));
    if (!ht) {
        return NULL;
    }

    ht->size_index = 0;
    ht->hash_size = hash_sizes[ht->size_index];

    ht->table = calloc(ht->hash_size.size, sizeof(struct hash_entry));
    if (!ht->table) {
        free(ht);
        return NULL;
    }

    ht->hash = hash;
    ht->equal = equal;
    ht->data = data;
    ht->entries = ht->deleted_entries = 0;

    return ht;
}

bool hash_table_set_max_entries(struct hash_table *ht, uint32_t max_entries)
{
    if (max_entries < ht->hash_size.max_entries) {
        return true;
    }
    for (uint32_t i = ht->size_index + 1; i < ARRAY_SIZE(hash_sizes); i++) {
        if (hash_sizes[i].max_entries >= max_entries) {
            hash_table_rehash(ht, i);
            break;
        }
    }
    return ht->hash_size.max_entries >= max_entries;
}

uint32_t hash_table_get_entries(const struct hash_table *ht)
{
    return ht->entries;
}

void hash_table_destroy(struct hash_table *ht)
{
    if (!ht) {
        return;
    }

    free(ht->table);
    free(ht);
}

struct hash_entry *hash_table_search_hash(const struct hash_table *ht, uint32_t hash,
                                          const void *key)
{
    uint32_t size = ht->hash_size.size;
    uint32_t start_hash_address = fast_urem32(hash, size, ht->hash_size.size_magic);
    uint32_t double_hash = 1 + fast_urem32(hash, ht->hash_size.rehash, ht->hash_size.rehash_magic);
    uint32_t hash_address = start_hash_address;

    do {
        struct hash_entry *entry = ht->table + hash_address;
        if (entry_is_free(entry)) {
            return NULL;
        } else if (entry_is_present(ht, entry) && entry->hash == hash) {
            if (ht->equal(key, entry->key, ht->data)) {
                return entry;
            }
        }
        hash_address += double_hash;
        if (hash_address >= size) {
            hash_address -= size;
        }
    } while (hash_address != start_hash_address);

    return NULL;
}

struct hash_entry *hash_table_search(const struct hash_table *ht, const void *key)
{
    return hash_table_search_hash(ht, ht->hash(key, ht->data), key);
}

static struct hash_entry *hash_table_get_entry(struct hash_table *ht, uint32_t hash,
                                               const void *key)
{
    if (ht->entries >= ht->hash_size.max_entries) {
        hash_table_rehash(ht, ht->size_index + 1);
    } else if (ht->deleted_entries + ht->entries >= ht->hash_size.max_entries) {
        hash_table_rehash(ht, ht->size_index);
    }

    uint32_t size = ht->hash_size.size;
    uint32_t start_hash_address = fast_urem32(hash, size, ht->hash_size.size_magic);
    uint32_t double_hash = 1 + fast_urem32(hash, ht->hash_size.rehash, ht->hash_size.rehash_magic);
    uint32_t hash_address = start_hash_address;
    struct hash_entry *available_entry = NULL;

    do {
        struct hash_entry *entry = ht->table + hash_address;

        if (!entry_is_present(ht, entry)) {
            /* Stash the first available entry we find */
            if (available_entry == NULL) {
                available_entry = entry;
            }
            if (entry_is_free(entry)) {
                break;
            }
        }

        /* already in the table */
        if (!entry_is_deleted(ht, entry) && entry->hash == hash &&
            ht->equal(key, entry->key, ht->data)) {
            return entry;
        }

        hash_address += double_hash;
        if (hash_address >= size) {
            hash_address -= size;
        }
    } while (hash_address != start_hash_address);

    if (available_entry) {
        if (entry_is_deleted(ht, available_entry)) {
            ht->deleted_entries--;
        }
        available_entry->hash = hash;
        ht->entries++;
        return available_entry;
    }

    return NULL;
}

struct hash_entry *hash_table_insert_hash(struct hash_table *ht, uint32_t hash, const void *key,
                                          void *data)
{
    struct hash_entry *entry = hash_table_get_entry(ht, hash, key);
    if (entry) {
        entry->key = key;
        entry->data = data;
    }
    return entry;
}

struct hash_entry *hash_table_insert(struct hash_table *ht, const void *key, void *data)
{
    return hash_table_insert_hash(ht, ht->hash(key, ht->data), key, data);
}

void hash_table_remove(struct hash_table *ht, struct hash_entry *entry)
{
    if (!entry) {
        return;
    }

    entry->key = DELETED_KEY_VALUE;
    ht->entries--;
    ht->deleted_entries++;
}

void hash_table_remove_hash(struct hash_table *ht, uint32_t hash, const void *key)
{
    hash_table_remove(ht, hash_table_search_hash(ht, hash, key));
}

void hash_table_remove_key(struct hash_table *ht, const void *key)
{
    hash_table_remove_hash(ht, ht->hash(key, ht->data), key);
}

struct hash_entry *hash_table_next_entry(struct hash_table *ht, struct hash_entry *entry)
{
    /* early return if table is empty */
    if (ht->entries == 0) {
        return NULL;
    }

    if (entry == NULL) {
        entry = ht->table;
    } else {
        entry = entry + 1;
    }

    for (; entry != ht->table + ht->hash_size.size; entry++) {
        if (entry_is_present(ht, entry)) {
            return entry;
        }
    }

    return NULL;
}

static uint32_t hash_pointer(const void *pointer, void *data)
{
    uintptr_t num = (uintptr_t)pointer;
    return (uint32_t)((num >> 2) ^ (num >> 6) ^ (num >> 10) ^ (num >> 14));
}

static bool hash_key_pointer_equal(const void *a, const void *b, void *data)
{
    return a == b;
}

struct hash_table *hash_table_create_pointer(void *data)
{
    return hash_table_create(hash_pointer, hash_key_pointer_equal, data);
}

uint32_t hash_string_with_length(const void *string, uint32_t length)
{
    uint32_t hash = 0x811C9DC5;
    const char *str = string;

    // Iterate over each character in the string
    for (uint32_t i = 0; i < length; i++) {
        // XOR the current byte with the hash value
        hash ^= (uint8_t)str[i];
        // Multiply by the FNV prime
        hash *= 0x01000193;
    }

    return hash;
}

static uint32_t hash_string(const void *string, void *data)
{
    return hash_string_with_length(string, strlen(string));
}

static bool hash_key_string_equal(const void *a, const void *b, void *data)
{
    return strcmp(a, b) == 0;
}

struct hash_table *hash_table_create_string(void *data)
{
    return hash_table_create(hash_string, hash_key_string_equal, data);
}

static uint32_t hash_int(const void *key, void *data)
{
    uint32_t hash = 0x811C9DC5;
    hash ^= (uintptr_t)key;
    hash *= 16777619;
    return hash;
}

static bool hash_key_int_equal(const void *a, const void *b, void *data)
{
    return (uintptr_t)a == (uintptr_t)b;
}

struct hash_table *hash_table_create_int(void *data)
{
    return hash_table_create(hash_int, hash_key_int_equal, data);
}
