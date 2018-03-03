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

#ifndef PH_HASHTABLE_H
#define PH_HASHTABLE_H

#include <pthread.h>

#include "Zend/zend_types.h"

#include "src/ph_debug.h"
#include "src/ph_string.h"

// #define PH_HT_FE(ht, key, value) \
    // for (int i = 0; i < ht.size; ++i) { \
        // ph_bucket_t *b = ht.values + i;

// hash table flags
#define FREE_KEYS 1 // @todo currently unused - still needed?

typedef struct _ph_bucket_t {
    ph_string_t *key; // @todo remove pointer to key?
    void *value;
    long hash;
    int variance;
} ph_bucket_t;

typedef struct _ph_hashtable_t {
    ph_bucket_t *values;
    int size;
    int used;
    int flags;
    void (*dtor)(void *);
    pthread_mutex_t lock;
} ph_hashtable_t;

void ph_hashtable_init(ph_hashtable_t *ht, int size, void (*dtor)(void *));
void ph_hashtable_insert(ph_hashtable_t *ht, ph_string_t *key, void *value);
void ph_hashtable_insert_ind(ph_hashtable_t *ht, long hash, void *value);
void ph_hashtable_delete(ph_hashtable_t *ht, ph_string_t *key);
void ph_hashtable_delete_ind(ph_hashtable_t *ht, long hash);
void *ph_hashtable_search(ph_hashtable_t *ht, ph_string_t *key);
void *ph_hashtable_search_ind(ph_hashtable_t *ht, long hash);
ph_string_t *ph_hashtable_key_fetch(ph_hashtable_t *ht, ph_string_t *key);
void ph_hashtable_update(ph_hashtable_t *ht, ph_string_t *key, void *value);
void ph_hashtable_update_ind(ph_hashtable_t *ht, long hash, void *value);
void ph_hashtable_destroy(ph_hashtable_t *ht);
void ph_hashtable_to_hashtable(HashTable *ht, ph_hashtable_t *phht);
void *ph_hashtable_random_value(ph_hashtable_t *ht);
void ph_hashtable_delete_n(ph_hashtable_t *ht, int n);

#endif
