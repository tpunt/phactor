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

ph_task_t *ph_task_create_send_message(ph_string_t *from_actor_ref, ph_string_t *to_actor_name, zval *message)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->type = PH_SEND_MESSAGE_TASK;
    ph_str_set(&new_task->u.smt.from_actor_ref, PH_STRV_P(from_actor_ref), PH_STRL_P(from_actor_ref));
    new_task->u.smt.to_actor_name = *to_actor_name;
    new_task->u.smt.message = ph_entry_create_from_zval(message);

    if (new_task->u.smt.message) {
        return new_task;
    }

    free(new_task);

    return NULL;
}

ph_task_t *ph_task_create_process_message(ph_actor_t *for_actor)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->u.pmt.for_actor = for_actor;
    new_task->type = PH_PROCESS_MESSAGE_TASK;

    return new_task;
}

ph_task_t *ph_task_create_resume_actor(ph_actor_t *actor)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->u.rat.actor = actor;
    new_task->type = PH_RESUME_ACTOR_TASK;

    return new_task;
}

ph_task_t *ph_task_create_new_actor(ph_string_t *named_actor_key, zend_string *class_name, zval *args, int argc)
{
    ph_task_t *new_task = malloc(sizeof(ph_task_t));

    new_task->u.nat.named_actor_key = named_actor_key;
    new_task->u.nat.args = NULL;
    new_task->u.nat.argc = argc;
    new_task->type = PH_NEW_ACTOR_TASK;

    if (argc) {
        new_task->u.nat.args = malloc(sizeof(ph_entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            if (!ph_entry_convert_from_zval(new_task->u.nat.args + i, args + i)) {
                zend_throw_error(NULL, "Failed to serialise argument %d of spawn()", i + 2);

                for (int i2 = 0; i2 < i; ++i2) {
                    ph_entry_value_free(new_task->u.nat.args + i2);
                }

                free(new_task->u.nat.args);
                free(new_task);
                return NULL;
            }
        }
    }

    ph_str_set(&new_task->u.nat.class_name, ZSTR_VAL(class_name), ZSTR_LEN(class_name));

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
            for (int i = 0; i < task->u.nat.argc; ++i) {
                ph_entry_value_free(task->u.nat.args + i);
            }
            free(task->u.nat.args);
        case PH_PROCESS_MESSAGE_TASK:
        case PH_RESUME_ACTOR_TASK:;
    }

    free(task);
}
