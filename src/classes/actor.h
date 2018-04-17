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
#include "src/ph_string.h"
#include "src/ds/ph_queue.h"
#include "src/ds/ph_hashtable.h"
#include "src/classes/supervisor.h"

struct _ph_thread_t;

typedef enum _ph_actor_state_t {
    PH_ACTOR_SPAWNING,     // prevents invoking receiveBlock in the constructor
    PH_ACTOR_IDLE,         // waiting for something - needs context restoring
    PH_ACTOR_ACTIVE,       // in execution
    PH_ACTOR_TERMINATED,   // actor terminated by supervisor - await for further action
    PH_ACTOR_CRASHED,      // await for further action (restart or free)
    PH_ACTOR_RESTARTING,   // actor is going through the restart cycle
    PH_ACTOR_BLOCKING,     // prevents an actor from being rescheduled via a new message
    PH_ACTOR_SHUTTING_DOWN // actor will be destroyed soon
} ph_actor_state_t;

typedef struct _ph_actor_internal_t {
    ph_string_t *ref;
    ph_context_t context;
    zend_object obj;
} ph_actor_internal_t;

typedef struct _ph_actor_t {
    ph_string_t *name;
    ph_string_t *ref; // duplicated (for now)
    ph_queue_t mailbox;
    ph_string_t class_name;
    ph_entry_t *ctor_args;
    int ctor_argc;
    struct _ph_thread_t *ph_thread;
    ph_actor_state_t state;
    int restart_count_streak;
    int restart_count;
    int tree_number;
    struct _ph_actor_t *supervisor;
    ph_supervision_t *supervision;
    pthread_mutex_t lock; // @todo remove this and just reuse mailbox lock
    ph_actor_internal_t *internal;
} ph_actor_t;

ph_actor_internal_t *ph_actor_internal_retrieve_from_object(zend_object *actor_obj);
ph_actor_t *ph_actor_retrieve_from_object(zend_object *actor_obj);
ph_actor_t *ph_actor_retrieve_from_zval(zval *actor_zval_obj);
ph_actor_t *ph_actor_create(ph_string_t *name, ph_string_t *ref, ph_string_t *class_name, ph_entry_t *ctor_args, int ctor_argc);
void ph_actor_ce_init(void);
void ph_actor_free(void *actor_void);
void ph_actor_free_dummy(void *actor_void);
zend_long ph_named_actor_removal(zend_string *name, zend_long count);
zend_long ph_named_actor_total(zend_string *name);
void ph_named_actor_remove(void *named_actor_void);
void ph_actor_mark_for_removal(void *actor_void);
int ph_valid_actor_arg(zval *to_actor, char *using_actor_name, ph_string_t *to_actor_name);
void ph_actor_internal_free(ph_actor_internal_t *actor_internal);
void ph_actor_remove_from_table(void *actor_void);

#endif
