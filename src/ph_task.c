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

#include "src/ph_task.h"

ph_task_t *ph_task_create_send_message(ph_string_t *from_actor_ref, ph_string_t *to_actor_name, int using_actor_name, zval *message)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->type = PH_SEND_MESSAGE_TASK;
    new_task->u.smt.from_actor_ref = *from_actor_ref;
    new_task->u.smt.to_actor_name = *to_actor_name;
    new_task->u.smt.message = ph_entry_create_from_zval(message);
    new_task->u.smt.using_actor_name = using_actor_name;

    if (new_task->u.smt.message) {
        return new_task;
    }

    free(new_task);

    return NULL;
}

ph_task_t *ph_task_create_resume_actor(ph_actor_t *actor)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    ph_str_set(&new_task->u.rat.actor_ref, PH_STRV_P(actor->internal->ref), PH_STRL_P(actor->internal->ref));
    new_task->type = PH_RESUME_ACTOR_TASK;

    return new_task;
}

ph_task_t *ph_task_create_new_actor(ph_string_t *actor_ref, ph_string_t *actor_class)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->type = PH_NEW_ACTOR_TASK;
    new_task->u.nat.actor_ref = actor_ref;
    new_task->u.nat.actor_class = *actor_class;

    return new_task;
}

void ph_task_free(void *task_void)
{
    ph_task_t *task = task_void;

    switch (task->type) {
        case PH_SEND_MESSAGE_TASK:
            ph_entry_free(task->u.smt.message);
            break;
        case PH_NEW_ACTOR_TASK:
        case PH_RESUME_ACTOR_TASK:;
    }

    free(task);
}
