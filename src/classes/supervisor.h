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

#ifndef PH_SUPERVISOR_H
#define PH_SUPERVISOR_H

#include "src/ds/ph_hashtable.h"

struct _ph_actor_t;

typedef enum _ph_supervision_strategies_t {
    PH_SUPERVISOR_ONE_FOR_ONE
} ph_supervision_strategies_t;

typedef struct _ph_supervision_t {
    ph_supervision_strategies_t strategy;
    ph_hashtable_t workers;
} ph_supervision_t;

typedef struct _ph_supervisor_t {
    ph_string_t ref;
    zend_object obj;
} ph_supervisor_t;

void ph_supervisor_ce_init(void);
void ph_supervisor_handle_crash(struct _ph_actor_t *supervisor, struct _ph_actor_t *crashed_actor);

#endif
