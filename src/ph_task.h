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

#ifndef PH_TASK_H
#define PH_TASK_H

#include "src/ph_string.h"
#include "src/ph_entry.h"
#include "src/classes/actor.h"

typedef enum _ph_task_type_t {
    PH_PROCESS_MESSAGE_TASK,
    PH_SEND_MESSAGE_TASK,
    PH_RESUME_ACTOR_TASK,
    PH_NEW_ACTOR_TASK
} ph_task_type_t;

typedef struct _ph_process_message_task_t {
    ph_actor_t *for_actor;
} ph_process_message_task_t;

typedef struct _ph_send_message_task_t {
    ph_string_t from_actor_ref;
    ph_string_t to_actor_name;
    ph_entry_t *message;
} ph_send_message_task_t;

typedef struct _ph_resume_actor_task_t {
    ph_actor_t *actor;
} ph_resume_actor_task_t;

typedef struct _ph_new_actor_task_t_t {
    ph_string_t *named_actor_key;
    ph_string_t class_name;
    ph_entry_t *args;
    int argc;
} ph_new_actor_task_t_t;

typedef struct _ph_task_t {
    union {
        ph_process_message_task_t pmt;
        ph_send_message_task_t smt;
        ph_resume_actor_task_t rat;
        ph_new_actor_task_t_t nat;
    } u;
    ph_task_type_t type;
} ph_task_t;

ph_task_t *ph_task_create_send_message(ph_string_t *from_actor_ref, ph_string_t *to_actor_name, zval *message);
ph_task_t *ph_task_create_process_message(ph_actor_t *for_actor);
ph_task_t *ph_task_create_resume_actor(ph_actor_t *actor);
ph_task_t *ph_task_create_new_actor(ph_string_t *named_actor_key, zend_string *class_name, zval *args, int argc);
void ph_task_free(void *task_void);

#endif
