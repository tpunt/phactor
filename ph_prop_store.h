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

struct _actor_t;

typedef struct _entry_t {
    int type;
    union {
        int boolean;
        int integer;
        double floating;
        ph_string_t string;
        zend_function *func;
        // struct _actor_t *actor;
        // array
        // object
        // resource ?
    } val;
    // uint32_t scope;
} entry_t;

#define PH_STORE_FUNC 100
#define PH_STORE_ACTOR 101

#define ENTRY_TYPE(s) (s)->type
#define ENTRY_STRING(s) (s)->val.string
#define ENTRY_LONG(s) (s)->val.integer
#define ENTRY_DOUBLE(s) (s)->val.floating
#define ENTRY_BOOL(s) (s)->val.boolean
#define ENTRY_FUNC(s) (s)->val.func

void ph_convert_entry_to_zval(zval *value, entry_t *s);
int ph_convert_zval_to_entry(entry_t *e, zval *value);
void ph_entry_delete(void *store);
void ph_entry_delete_value(entry_t *entry);
entry_t *create_new_entry(zval *value);

#endif
