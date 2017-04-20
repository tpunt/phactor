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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ph_general.h"
#include "ph_prop_store.h"

static void ph_store_convert(zval *value, store_t *s);
static store_t *create_new_store(zval *value);

void ph_store_add(ph_hashtable_t *ht, zend_string *name, zval *value)
{
    ph_string_t key;

    ph_string_update(&key, ZSTR_VAL(name), ZSTR_LEN(name));
    ph_hashtable_insert(ht, &key, create_new_store(value));
}

void ph_store_hashtable_convert(HashTable *ht, ph_hashtable_t *props)
{
    for (int i = 0; i < props->size; ++i) {
		ph_bucket_t *b = props->values + i;
		store_t *s = b->value;
		zval value;

		if (b->hash < 1) {
			continue;
		}

		ph_store_convert(&value, s);

		_zend_hash_str_add(ht, PH_STRV(b->key), PH_STRL(b->key), &value ZEND_FILE_LINE_CC);
	}
}

void ph_store_read(actor_t *actor, zend_string *key, zval **rv)
{
    ph_string_t phstr;

    PH_STRV(phstr) = ZSTR_VAL(key);
    PH_STRL(phstr) = ZSTR_LEN(key);

    store_t *s = ph_hashtable_search(&actor->props, &phstr);

    if (s) {
        ph_store_convert(*rv, s);
    } else {
        // @todo
    }
}

void delete_store(void *store_void)
{
    store_t *store = (store_t *) store_void;

    free(store);
}

static void ph_store_convert(zval *value, store_t *s)
{
    switch (s->type) {
        case IS_STRING:
            ZVAL_NEW_STR(value, s->val.string);
            break;
        case IS_LONG:
            ZVAL_LONG(value, s->val.integer);
            break;
        case IS_DOUBLE:
            ZVAL_DOUBLE(value, s->val.floating);
            break;
        case _IS_BOOL:
            ZVAL_BOOL(value, s->val.boolean);
            break;
        // obj...
    }
}

static store_t *create_new_store(zval *value)
{
    store_t *s = malloc(sizeof(store_t));

    STORE_TYPE(s) = Z_TYPE_P(value);

    switch (Z_TYPE_P(value)) {
        case IS_STRING:
            STORE_STRING(s) = Z_STR_P(value); // @todo should be a copy of zend string
            break;
        case IS_LONG:
            STORE_LONG(s) = Z_LVAL_P(value);
            break;
        case IS_DOUBLE:
            STORE_DOUBLE(s) = Z_DVAL_P(value);
            break;
        case _IS_BOOL:
            STORE_BOOL(s) = !!Z_LVAL_P(value);
            break;
    }

    return s;
}
