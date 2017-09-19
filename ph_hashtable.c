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

static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, void *value);
static void ph_hashtable_resize(ph_hashtable_t *ht);
static void ph_hashtable_rehash(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size);
static int get_hash(ph_string_t *key);

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
        free(PH_STRV(b->key));
    }

    free(ht->values);
}

void ph_hashtable_insert(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    if (ph_hashtable_search(ht, key)) {
        ph_hashtable_update(ht, key, value);
    } else {
        // resize at 75% capacity
        if (ht->n_used == ht->size - (ht->size >> 2)) {
            ph_hashtable_resize(ht);
        }

        ph_hashtable_insert_direct(ht, key, value);
    }
}

static void ph_hashtable_insert_direct(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    int hash = get_hash(key);
    int index = hash & (ht->size - 1);
    int variance = 0;
    ph_string_t tmp_key; // ugly hack...

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash < 1) {
            b->hash = hash;
            // b->key = *key; // ?
            PH_STRV(b->key) = PH_STRV_P(key);
            PH_STRL(b->key) = PH_STRL_P(key);
            b->value = value;
            b->variance = variance;

            break;
        }

        if (variance > b->variance) {
            int tmp_hash = b->hash;
            void *tmp_value = b->value;
            int tmp_variance = b->variance;

            tmp_key = b->key;

            b->hash = hash;
            PH_STRV(b->key) = PH_STRV_P(key);
            PH_STRL(b->key) = PH_STRL_P(key);
            b->value = value;
            b->variance = variance;

            hash = tmp_hash;
            key = &tmp_key;
            value = tmp_value;
            variance = tmp_variance;
        } else {
            ++variance;
        }

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
    ht->values = malloc(sizeof(ph_bucket_t) * ht->size);

    ph_hashtable_rehash(ht, old_values, old_size);

    free(old_values);
}

static void ph_hashtable_rehash(ph_hashtable_t *ht, ph_bucket_t *old_values, int old_size)
{
    for (int i = 0; i < old_size; ++i) {
        if (old_values[i].hash < 1) {
            continue;
        }

        ph_hashtable_insert_direct(ht, &old_values[i].key, old_values[i].value);
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

void *ph_hashtable_search(ph_hashtable_t *ht, ph_string_t *key)
{
    int hash = get_hash(key);
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == -1) {
            continue;
        }

        if (b->hash == hash && strcmp(PH_STRV(b->key), PH_STRV_P(key)) == 0) {
            return b->value;
        }

        if (b->hash == 0) {
            return NULL;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }

    return NULL;
}

void ph_hashtable_update(ph_hashtable_t *ht, ph_string_t *key, void *value)
{
    int hash = get_hash(key);
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == hash && strcmp(PH_STRV(b->key), PH_STRV_P(key)) == 0) {
            // @todo free previous value?
            b->value = value;
            break;
        }

        if (b->hash == 0) { // actor not found!
            break;
        }

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index -= ht->size;
        }
    }
}

void ph_hashtable_delete(ph_hashtable_t *ht, ph_string_t *key, void (*dtor_value)(void *))
{
    int hash = get_hash(key);
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        ph_bucket_t *b = ht->values + index;

        if (b->hash == hash && strcmp(PH_STRV(b->key), PH_STRV_P(key)) == 0) {
            dtor_value(b->value);

            b->hash = -1; // -1 = tombstone
            PH_STRV(b->key) = NULL;
            PH_STRL(b->key) = 0;
            b->value = NULL;
            b->variance = 0;
            --ht->n_used;

            // @todo implement backtracking?

            break;
        }

        if (b->hash == 0) { // actor not found!
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

        ph_entry_convert(&value, b->value);

        _zend_hash_str_add(ht, PH_STRV(b->key), PH_STRL(b->key), &value ZEND_FILE_LINE_CC);
    }
}
