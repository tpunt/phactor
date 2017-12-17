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

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ph_debug.h"
#include "php_phactor.h"

void debug_tasks(task_t *task)
{
    int task_count = 0;

    printf("Debugging tasks:\n");

    while (task != NULL) {
        ++task_count;

        printf("%d) [%s] Task: %p ", task_count, task->task_type & PROCESS_MESSAGE_TASK ? "PMT" : "SMT", task);

        if (task->task_type & PROCESS_MESSAGE_TASK) {
            printf("(for Actor: %s, Message: {from_actor_ref = %s, message = %p})",
                PH_STRV(task->task.pmt.for_actor->ref) + 28, PH_STRV(task->task.pmt.for_actor->mailbox->from_actor_ref) + 28, task->task.pmt.for_actor->mailbox->message);
        } else {
            printf("(to Actor: %s, Message: {from_actor_ref = %s, message = %p})",
            PH_STRV(task->task.smt.to_actor_name) + 28, PH_STRV(task->task.smt.from_actor_ref) + 28, task->task.smt.message);
        }

        printf("\n");

        task = task->next_task;
    }

    printf("\n");
}

void debug_actor_system(actor_system_t *actor_system)
{
    ph_hashtable_t actors_ht = actor_system->actors;
    int actor_count = 0;

    printf("Debugging actors:\n");

    for (int i = 0; i < actors_ht.size; ++i) {
        ph_bucket_t b = actors_ht.values[i];

        if (b.hash > 0) {
            actor_t *actor = b.value;
            printf("%d) actor: %p (ref=%s, len=%d), object ref: %u\n", ++actor_count, actor, PH_STRV(actor->ref), PH_STRL(actor->ref), actor->obj.handle);
        }
    }

    printf("\n");
}
