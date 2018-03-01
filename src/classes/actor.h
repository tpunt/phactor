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

#ifndef PH_ACTOR_H
#define PH_ACTOR_H

#include "src/ph_context.h"
#include "src/ph_entry.h"
#include "src/ds/ph_queue.h"
#include "src/ph_string.h"
#include "src/ds/ph_hashtable.h"

typedef enum _ph_actor_state_t {
    PH_ACTOR_IDLE,  // waiting for something - needs context restoring
    PH_ACTOR_ACTIVE // in execution - prevents parallel execution of an actor
} ph_actor_state_t;

typedef struct _ph_actor_t {
    ph_string_t *ref;
    ph_string_t *name;
    ph_queue_t mailbox;
    ph_context_t context;
    ph_actor_state_t state;
    int thread_offset;
    pthread_mutex_t lock;
    zend_object obj;
} ph_actor_t;

ph_actor_t *ph_actor_retrieve_from_object(zend_object *actor_obj);
ph_actor_t *ph_actor_retrieve_from_zval(zval *actor_zval_obj);
void ph_actor_ce_init(void);
void ph_actor_free(void *actor_void);
void ph_actor_free_dummy(void *actor_void);
zend_long ph_named_actor_removal(zend_string *name, zend_long count);
zend_long ph_named_actor_total(zend_string *name);
void ph_named_actor_remove(void *named_actor_void);
void ph_actor_mark_for_removal(void *actor_void);

#endif
