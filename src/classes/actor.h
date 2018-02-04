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
#include "src/classes/common.h"

typedef enum _ph_actor_state_t {
    PH_ACTOR_NEW,   // not waiting - starts a fresh context
    PH_ACTOR_IDLE,  // waiting for something - needs context restoring
    PH_ACTOR_ACTIVE // in execution - prevents parallel execution of an actor
} ph_actor_state_t;

typedef struct _ph_actor_t {
    ph_string_t ref;
    ph_string_t *name;
    ph_queue_t mailbox;
    zend_execute_data *vm_stack; // vm_context
    zend_executor_globals eg;
    ph_context_t actor_context; // c_context
    ph_actor_state_t state;
    ph_entry_t *retval; // @todo make singly-linked list?
    int thread_offset;
    zend_object obj;
} ph_actor_t;

typedef enum _ph_named_actor_state_t {
    PH_NAMED_ACTOR_CONSTRUCTING,
    PH_NAMED_ACTOR_ACTIVE
} ph_named_actor_state_t;

typedef struct _ph_named_actor_t {
    // ph_string_t name; // needed?
    ph_named_actor_state_t state;
    int perceived_used; // for the calling code
    ph_hashtable_t actors;
    // mutex lock?
} ph_named_actor_t;

ph_actor_t *ph_actor_retrieve_from_name(ph_string_t *actor_name);
ph_actor_t *ph_actor_retrieve_from_ref(ph_string_t *actor_ref);
void ph_actor_ce_init(void);
void ph_actor_free(void *actor_void);
void ph_actor_remove(void *target_actor_void);

extern pthread_mutex_t global_actor_id_lock;

#endif
