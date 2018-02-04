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

#ifndef PH_ENTRY_H
#define PH_ENTRY_H

#include "Zend/zend.h"

#include "src/ph_string.h"
#include "src/ds/ph_hashtable.h"

typedef struct _ph_entry_t {
    int type;
    union {
        int boolean;
        int integer;
        double floating;
        ph_string_t string;
        zend_function *func;
    } u;
} ph_entry_t;

#define PH_STORE_FUNC 100

#define PH_ENTRY_TYPE(s) (s)->type
#define PH_ENTRY_STRING(s) (s)->u.string
#define PH_ENTRY_LONG(s) (s)->u.integer
#define PH_ENTRY_DOUBLE(s) (s)->u.floating
#define PH_ENTRY_BOOL(s) (s)->u.boolean
#define PH_ENTRY_FUNC(s) (s)->u.func

void ph_entry_convert_to_zval(zval *value, ph_entry_t *entry);
int ph_entry_convert_from_zval(ph_entry_t *entry, zval *value);
void ph_entry_free(void *entry_void);
void ph_entry_value_free(ph_entry_t *entry);
ph_entry_t *ph_entry_create_from_zval(zval *value);

#endif
