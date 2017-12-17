/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

#include <stdlib.h>
#include <string.h>

#include "ph_prop_store.h"
#include "ph_hashtable.h"

static void *ph_hashtable_search_direct(ph_hashtable_t *ht, ph_string_t *key, int hash);
static ph_string_t *ph_hashtable_key_fetch_direct(ph_hashtable_t *ht, ph_string_t *key, int hash);
static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void *value);
void ph_hashtable_update_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void *value);
void ph_hashtable_delete_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void (*dtor_value)(void *));
static void ph_hashtable_resize(ph_hashtable_t *ht);
static void ph_hashtable_rehash(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size);
static int get_hash(ph_string_t *key);

// @todo unused
ph_hashtable_t *ph_hashtable_alloc(int size)
{
    ph_hashtable_t *ht = calloc(sizeof(ph_hashtable_t), 1);

    ph_hashtable_init(ht, size);

    return ht;
}

void ph_hashtable_init(ph_hashtable_t *ht, int size)
{
    ht->values = calloc(sizeof(ph_bucket_t), size);
    ht->size = size;
    ht->n_used = 0;
}

void ph_hashtable_destroy(ph_hashtable_t *ht, void (*dtor_value)(void *))
{
    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + i;

        if (b->hash < 1) {
            continue;
        }

        dtor_value(b->value);

        if (ht->flags & FREE_KEYS) {
            free(PH_STRV_P(b->key));
            free(b->key);
        }
    }

    free(ht->values);
}

void ph_hashtable_insert_ind(ph_hashtable_t *ht, int hash, void *value)
{
    // resize at 75% capacity
    if (ht->n_used == ht->size - (ht->size >> 2)) {
        ph_hashtable_resize(ht);
    }

    ph_hashtable_insert_direct(ht, NULL, hash, value);
}

void ph_hashtable_insert(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    // resize at 75% capacity
    if (ht->n_used == ht->size - (ht->size >> 2)) {
        ph_hashtable_resize(ht);
    }

    ph_hashtable_insert_direct(ht, key, get_hash(key), value);
}

static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void *value)
{
    int index = hash & (ht->size - 1);
    int variance = 0;

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash < 1) {
            b->hash = hash;
            b->key = key;
            b->value = value;
            b->variance = variance;

            break;
        }

        if (variance > b->variance) {
            int tmp_hash = b->hash;
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
            index -= ht->size;
        }
    }

    ++ht->n_used;
}

static void ph_hashtable_resize(ph_hashtable_t *ht)
{
    ph_bucket_t *old_values = ht->values;
    int old_size = ht->size;

    ht->size <<= 1;
    ht->n_used = 0;
    ht->values = calloc(sizeof(ph_bucket_t), ht->size);

    ph_hashtable_rehash(ht, old_values, old_size);

    free(old_values);
}

static void ph_hashtable_rehash(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size)
{
    for (int i = 0; i < old_size; ++i) {
        if (old_values[i].hash < 1) {
            continue;
        }

        ph_hashtable_insert_direct(ht, old_values[i].key, old_values[i].hash, old_values[i].value);
    }
}

// @todo make decent
static int get_hash(ph_string_t *key)
{
    int hash = 0;

    for (int i = 0; i < PH_STRL_P(key); ++i) {
        hash += PH_STRV_P(key)[i];
    }

    return hash;
}

void *ph_hashtable_search_ind(ph_hashtable_t *ht, int hash)
{
    return ph_hashtable_search_direct(ht, NULL, hash);
}

void *ph_hashtable_search(ph_hashtable_t *ht, ph_string_t *key)
{
    return ph_hashtable_search_direct(ht, key, get_hash(key));
}

void *ph_hashtable_search_direct(ph_hashtable_t *ht, ph_string_t *key, int hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == -1) {
            continue;
        }

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
            return b->value;
        }

        if (b->hash == 0) { // @todo when can the hash ever be 0? If never, then this should go before above condition
            return NULL;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }

    return NULL;
}

ph_string_t *ph_hashtable_key_fetch(ph_hashtable_t *ht, ph_string_t *key)
{
    return ph_hashtable_key_fetch_direct(ht, key, get_hash(key));
}

static ph_string_t *ph_hashtable_key_fetch_direct(ph_hashtable_t *ht, ph_string_t *key, int hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == -1) {
            continue;
        }

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
            return b->key;
        }

        if (b->hash == 0) { // @todo when can the hash ever be 0? If never, then this should go before above condition
            return NULL;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }

    return NULL;
}

void ph_hashtable_update_ind(ph_hashtable_t *ht, int hash, void *value)
{
    ph_hashtable_update_direct(ht, NULL, hash, value);
}

void ph_hashtable_update(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    ph_hashtable_update_direct(ht, key, get_hash(key), value);
}

void ph_hashtable_update_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void *value)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
            // @todo free previous value?
            b->value = value;
            break;
        }

        if (b->hash == 0) {
            break;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }
}

void ph_hashtable_delete_ind(ph_hashtable_t *ht, int hash, void (*dtor_value)(void *))
{
    ph_hashtable_delete_direct(ht, NULL, hash, dtor_value);
}

void ph_hashtable_delete(ph_hashtable_t *ht, ph_string_t *key, void (*dtor_value)(void *))
{
    ph_hashtable_delete_direct(ht, key, get_hash(key), dtor_value);
}

void ph_hashtable_delete_direct(ph_hashtable_t *ht, ph_string_t *key, int hash, void (*dtor_value)(void *))
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || ph_str_eq(b->key, key))) {
            dtor_value(b->value);

            b->hash = -1; // -1 = tombstone

            if (ht->flags & FREE_KEYS) {
                free(PH_STRV_P(b->key));
                free(b->key);
            }

            b->key = NULL;
            b->value = NULL;
            b->variance = 0;
            --ht->n_used;

            // @todo implement backtracking?

            break;
        }

        if (b->hash == 0) {
            break;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }
}

void ph_hashtable_to_hashtable(HashTable *ht, ph_hashtable_t *phht)
{
    for (int i = 0; i < phht->size; ++i) {
        ph_bucket_t *b = phht->values + i;
        zval value;

        if (b->hash < 1) {
            continue;
        }

        ph_convert_entry_to_zval(&value, b->value);

        _zend_hash_str_add(ht, PH_STRV_P(b->key), PH_STRL_P(b->key), &value ZEND_FILE_LINE_CC);
    }
}

void *ph_hashtable_random_value(ph_hashtable_t *ht)
{
    assert(ht->n_used);

    // @todo improve?
    do {
        int i = rand() % ht->size; // @todo modulo bias

        if (ht->values[i].hash > 0) {
            return ht->values[i].value;
        }
    } while (1);
}
