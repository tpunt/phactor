/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-present The PHP Group                             |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Thomas Punt <tpunt@php.net>                                  |
  +----------------------------------------------------------------------+
*/

#include <stdlib.h>
#include <string.h>

#include "src/ph_entry.h"
#include "src/ds/ph_hashtable.h"

static void *ph_hashtable_search_direct(ph_hashtable_t *ht, ph_string_t *key, long hash);
static ph_string_t *ph_hashtable_key_fetch_direct(ph_hashtable_t *ht, ph_string_t *key, long hash);
static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, long hash, void *value);
void ph_hashtable_update_direct(ph_hashtable_t *ht, ph_string_t *key, long hash, void *value);
void ph_hashtable_delete_direct(ph_hashtable_t *ht, ph_string_t *key, long hash);
static void ph_hashtable_resize(ph_hashtable_t *ht);
static void ph_hashtable_repopulate(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size);
static long get_hash(ph_string_t *key);

void ph_hashtable_init(ph_hashtable_t *ht, int size, void (*dtor)(void *))
{
    ht->values = calloc(sizeof(ph_bucket_t), size);
    ht->size = size;
    ht->used = 0;
    ht->flags = 0;
    ht->dtor = dtor;
    pthread_mutex_init(&ht->lock, NULL);
}

void ph_hashtable_clear(ph_hashtable_t *ht)
{
    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + i;

        if (!b->value) {
            continue;
        }

        ht->dtor(b->value);

        if (b->key && ht->flags & FREE_KEYS) {
            ph_str_free(b->key);
        }

        b->key = NULL;
        b->hash = 0;
        b->value = NULL;
        b->variance = 0;
    }

    ht->used = 0;
}

void ph_hashtable_destroy(ph_hashtable_t *ht)
{
    ph_hashtable_clear(ht);
    free(ht->values);
    pthread_mutex_destroy(&ht->lock);
}

void ph_hashtable_apply(ph_hashtable_t *ht, void (*apply)(void *))
{
    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + i;

        if (!b->value) {
            continue;
        }

        apply(b->value);
    }
}

void ph_hashtable_insert_ind(ph_hashtable_t *ht, long hash, void *value)
{
    // resize at 75% capacity
    if (ht->used == ht->size - (ht->size >> 2)) {
        ph_hashtable_resize(ht);
    }

    ph_hashtable_insert_direct(ht, NULL, hash, value);
}

void ph_hashtable_insert(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    // resize at 75% capacity
    if (ht->used == ht->size - (ht->size >> 2)) {
        ph_hashtable_resize(ht);
    }

    ph_hashtable_insert_direct(ht, key, get_hash(key), value);
}

static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, long hash, void *value)
{
    int index = hash & (ht->size - 1);
    int variance = 0;

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (!b->value) {
            b->hash = hash;
            b->key = key;
            b->value = value;
            b->variance = variance;

            break;
        }

        if (variance > b->variance) {
            long tmp_hash = b->hash;
            void *tmp_value = b->value;
            int tmp_variance = b->variance;
            ph_string_t *tmp_key = b->key;

            b->hash = hash;
            b->key = key;
            b->value = value;
            b->variance = variance;

            hash = tmp_hash;
            key = tmp_key;
            value = tmp_value;
            variance = tmp_variance;
        }

        ++variance;

        if (++index == ht->size) {
            index = 0;
        }
    }

    ++ht->used;
}

static void ph_hashtable_resize(ph_hashtable_t *ht)
{
    ph_bucket_t *old_values = ht->values;
    int old_size = ht->size;

    ht->size <<= 1;
    ht->used = 0;
    ht->values = calloc(sizeof(ph_bucket_t), ht->size);

    ph_hashtable_repopulate(ht, old_values, old_size);

    free(old_values);
}

static void ph_hashtable_repopulate(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size)
{
    for (int i = 0; i < old_size; ++i) {
        if (old_values[i].value) {
            ph_hashtable_insert_direct(ht, old_values[i].key, old_values[i].hash, old_values[i].value);
        }
    }
}

static long get_hash(ph_string_t *key)
{
    return zend_hash_func(PH_STRV_P(key), PH_STRL_P(key));
}

void *ph_hashtable_search_ind(ph_hashtable_t *ht, long hash)
{
    return ph_hashtable_search_direct(ht, NULL, hash);
}

void *ph_hashtable_search(ph_hashtable_t *ht, ph_string_t *key)
{
    return ph_hashtable_search_direct(ht, key, get_hash(key));
}

void *ph_hashtable_search_direct(ph_hashtable_t *ht, ph_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
                return b->value;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return NULL;
        } // @todo if backtracking was implemented then !b->value could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }

    return NULL;
}

ph_string_t *ph_hashtable_key_fetch(ph_hashtable_t *ht, ph_string_t *key)
{
    return ph_hashtable_key_fetch_direct(ht, key, get_hash(key));
}

static ph_string_t *ph_hashtable_key_fetch_direct(ph_hashtable_t *ht, ph_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
                return b->key;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return NULL;
        } // @todo if backtracking was implemented then !b->value could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }

    return NULL;
}

void ph_hashtable_update_ind(ph_hashtable_t *ht, long hash, void *value)
{
    ph_hashtable_update_direct(ht, NULL, hash, value);
}

void ph_hashtable_update(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    ph_hashtable_update_direct(ht, key, get_hash(key), value);
}

void ph_hashtable_update_direct(ph_hashtable_t *ht, ph_string_t *key, long hash, void *value)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
            // @todo free previous value?
            b->value = value;
            break;
        }

        if (++index == ht->size) {
            index = 0;
        }
    }
}

void ph_hashtable_delete_ind(ph_hashtable_t *ht, long hash)
{
    ph_hashtable_delete_direct(ht, NULL, hash);
}

void ph_hashtable_delete(ph_hashtable_t *ht, ph_string_t *key)
{
    ph_hashtable_delete_direct(ht, key, get_hash(key));
}

void ph_hashtable_delete_direct(ph_hashtable_t *ht, ph_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
                ht->dtor(b->value);

                if (ht->flags & FREE_KEYS) {
                    ph_str_free(b->key);
                }

                b->key = NULL;
                b->hash = 1; // tombstone
                b->value = NULL;
                b->variance = 0;
                --ht->used;

                // @todo implement backtracking?

                break;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return;
        } // @todo if backtracking was implemented then !b->value could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }
}

void ph_hashtable_delete_n(ph_hashtable_t *ht, int n)
{
    for (int i = 0; i < ht->size && ht->used && n; ++i) {
        ph_bucket_t *b = ht->values + i;

        if (b->value) {
            ht->dtor(b->value);

            if (ht->flags & FREE_KEYS) {
                ph_str_free(b->key);
            }

            b->key = NULL;
            b->hash = 1; // tombstone
            b->value = NULL;
            b->variance = 0;
            --ht->used;
            --n;

            // @todo implement backtracking?
        }
    }

    // @todo hash table downsizing?
}

void ph_hashtable_to_hashtable(HashTable *ht, ph_hashtable_t *phht)
{
    for (int i = 0; i < phht->size; ++i) {
        ph_bucket_t *b = phht->values + i;
        zval value;

        if (!b->value) {
            continue;
        }

        ph_entry_convert_to_zval(&value, b->value);

        if (b->key) {
            _zend_hash_str_add(ht, PH_STRV_P(b->key), PH_STRL_P(b->key), &value ZEND_FILE_LINE_CC);
        } else {
            _zend_hash_index_add(ht, b->hash, &value ZEND_FILE_LINE_CC);
        }
    }
}

void *ph_hashtable_random_value(ph_hashtable_t *ht)
{
    assert(ht->used);

    // @todo improve?
    do {
        int i = rand() % ht->size; // @todo modulo bias

        if (ht->values[i].value) {
            return ht->values[i].value;
        }
    } while (1);
}
