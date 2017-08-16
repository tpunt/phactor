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

#ifndef PH_PROP_STORE_H
#define PH_PROP_STORE_H

#include "Zend/zend.h"
#include "ph_general.h"
#include "ph_hashtable.h"

typedef struct _store_t {
    zend_class_entry *ce;
    ph_hashtable_t props;
} store_t;

typedef struct _entry_t {
    int type;
    union {
        int boolean;
        int integer;
        double floating;
        ph_string_t string;
        // array
        // object
        // resource ?
    } val;
    uint32_t scope;
} entry_t;

#define ENTRY_TYPE(s) (s)->type
#define ENTRY_STRING(s) (s)->val.string
#define ENTRY_LONG(s) (s)->val.integer
#define ENTRY_DOUBLE(s) (s)->val.floating
#define ENTRY_BOOL(s) (s)->val.boolean
#define ENTRY_SCOPE(s) (s)->scope

void ph_store_add(store_t *store, zend_string *name, zval *value, uint32_t scope);
void ph_store_to_hashtable(HashTable *ht, store_t *store);
void ph_entry_convert(zval *value, entry_t *s);
void ph_store_read(store_t *store, zend_string *key, zval **rv, zval *this);
void delete_entry(void *store);
entry_t *create_new_entry(zval *value, uint32_t scope);

#endif
