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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_phactor.h"
#include "src/ph_task.h"
#include "src/ph_debug.h"
#include "src/classes/actor_system.h"

extern ph_actor_system_t *actor_system;

char *get_task_type(ph_task_type_t type)
{
    switch (type) {
        case PH_SEND_MESSAGE_TASK:
            return "SMT";
        case PH_RESUME_ACTOR_TASK:
            return "RAT";
        case PH_NEW_ACTOR_TASK:
            return "NAT";
        default:
            ZEND_ASSERT(0);
    }
}
/*
void ph_debug_tasks(ph_queue_t *tasks)
{
    int task_count = 0;

    printf("Debugging tasks:\n");

    linked_list_t *ll = tasks->elements;

    for (int i = 0; ll; ++i, ll = ll->next) {
        ph_task_t *task = ll->element;

        ++task_count;

        printf("%d) [%s] Task: %p ", task_count, get_task_type(task->type), task);

        switch (task->type) {
            case PH_SEND_MESSAGE_TASK:
                printf("(to Actor: %s, Message: {from_actor_ref = %s, message = %p})",
                    PH_STRV(task->u.smt.to_actor_name) + 28, PH_STRV(task->u.smt.from_actor_ref) + 28, task->u.smt.message);
                break;
            case PH_RESUME_ACTOR_TASK:
            case PH_NEW_ACTOR_TASK:
                // ...
                ;
        }

        printf("\n");
    }

    printf("\n");
}

void ph_debug_actor_system(void)
{
    ph_hashtable_t actors_ht = PHACTOR_G(actor_system)->actors;
    int actor_count = 0;

    printf("Debugging actors:\n");

    for (int i = 0; i < actors_ht.size; ++i) {
        ph_bucket_t b = actors_ht.values[i];

        if (b.hash > 0) {
            ph_actor_t *actor = b.value;
            printf("%d) actor: %p (ref=%s, len=%d), object ref: %u\n", ++actor_count, actor, PH_STRV(actor->ref), PH_STRL(actor->ref), actor->obj.handle);
        }
    }

    printf("\n");
}
*/
