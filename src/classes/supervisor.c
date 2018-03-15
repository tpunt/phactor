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

#include <main/php.h>
#include <Zend/zend_exceptions.h>
#include "ext/standard/php_mt_rand.h"

#include "php_phactor.h"
#include "src/ph_task.h"
#include "src/ph_string.h"
#include "src/classes/actor.h"
#include "src/classes/actor_ref.h"
#include "src/classes/supervisor.h"
#include "src/classes/actor_system.h"

extern ph_actor_system_t *actor_system;
extern zend_class_entry *ph_Actor_ce;
extern zend_class_entry *ph_ActorRef_ce;

zend_object_handlers ph_Supervisor_handlers;
zend_class_entry *ph_Supervisor_ce;
pthread_mutex_t global_tree_number_lock;
int global_tree_number;

ph_supervisor_t *ph_supervisor_fetch_from_object(zend_object *supervisor_obj)
{
    return (ph_supervisor_t *)((char *)supervisor_obj - supervisor_obj->handlers->offset);
}

void ph_supervisor_one_for_one(void *crashed_actor_void)
{
    ph_actor_t *crashed_actor = crashed_actor_void;

    // @todo mutex lock here is likely not needed, since only
    // this thread will touch these members
    pthread_mutex_lock(&crashed_actor->lock);
    if (crashed_actor->state == PH_ACTOR_SPAWNING) {
        // Hit when a supervisor is being spawned with at least one worker (RC).
        // The new_actor function needs to invoke this function for any workers,
        // in case we are going through a supervision tree restart.
        pthread_mutex_unlock(&crashed_actor->lock);
        return;
    }
    ph_actor_internal_free(crashed_actor->internal);
    crashed_actor->internal = NULL;
    crashed_actor->state = PH_ACTOR_SPAWNING;
    pthread_mutex_unlock(&crashed_actor->lock);

    ph_task_t *task = ph_task_create_new_actor(crashed_actor->ref, &crashed_actor->class_name);

    // @todo we don't have to schedule the actor to be on the same thread, but
    // for now, we will do
    // An advantage of scheduling on the same thread is that we could avoid
    // deallocating, and then reallocating, the virtual machine stack (just
    // reset it instead). If we pooled such things, then it shouldn't matter...
    // Static member values will only remain the same on the same thread!
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + crashed_actor->thread_offset;

    pthread_mutex_lock(&thread->tasks.lock);
    ph_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->tasks.lock);
}

void ph_supervisor_dfs_apply(ph_actor_t *supervisor, void (*apply)(void *))
{
    if (supervisor->supervision) {
        ph_hashtable_apply(&supervisor->supervision->workers, apply);
    }
}

void ph_supervisor_handle_crash(ph_actor_t *supervisor, ph_actor_t *crashed_actor)
{
    if (++crashed_actor->restart_count_streak == supervisor->supervision->restart_count_streak_max) {
        // @todo how should we react? Log it? Crash the supervisor?
        // default to crashing the supervisor
        // in future, add the abililty to use different restart strategies
        ph_actor_crash(supervisor);
        return;
    }

    switch (supervisor->supervision->strategy) {
        case PH_SUPERVISOR_ONE_FOR_ONE:
            ph_supervisor_one_for_one(crashed_actor);
            break;
    }
}

void ph_actor_terminate_workers(void *actor_void)
{
    ph_actor_t *actor = actor_void;

    pthread_mutex_lock(&actor->lock);
    actor->state = PH_ACTOR_TERMINATED;
    pthread_mutex_unlock(&actor->lock);
}

// belongs in actor.c ?
void ph_actor_crash(ph_actor_t *actor)
{
    if (actor->state == PH_ACTOR_CRASHED) {
        return;
    }

    ph_supervisor_dfs_apply(actor, ph_actor_terminate_workers);

    pthread_mutex_lock(&actor->lock);
    actor->state = PH_ACTOR_CRASHED;
    pthread_mutex_unlock(&actor->lock);

    if (actor->supervisor) {
        ph_supervisor_handle_crash(actor->supervisor, actor);
    } else {
        // @todo log crash here

        // dfs post-order traversal to terminate any workers, and then free them
        pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
        ph_supervisor_dfs_apply(actor, ph_actor_remove_from_table);
        ph_actor_free(actor);
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    }
}

void ph_supervision_tree_create(ph_actor_t *supervisor, ph_supervision_strategies_t strategy)
{
    int size = 2;

    // we are already holding the actors_by_ref lock
    pthread_mutex_lock(&supervisor->lock);

    supervisor->supervision = malloc(sizeof(ph_supervision_t));
    supervisor->supervision->strategy = strategy;
    supervisor->supervision->restart_count_streak_max = 5; // @todo should be configurable
    ph_hashtable_init(&supervisor->supervision->workers, 1, ph_actor_free_dummy);

    pthread_mutex_lock(&global_tree_number_lock);
    supervisor->supervision->tree_number = global_tree_number++;
    pthread_mutex_unlock(&global_tree_number_lock);

    pthread_mutex_unlock(&supervisor->lock);
}

void dummy_queue_dtor(void *v) {}

int ph_supervisor_cycle_detection(ph_actor_t *supervisor, ph_actor_t *worker)
{
    if (!worker->supervision) {
        return 0;
    }

    int cond = 1;
    int bad_tree_number = supervisor->supervision->tree_number;
    ph_queue_t queue;

    ph_queue_init(&queue, dummy_queue_dtor); // could be NULL instead
    ph_queue_push(&queue, worker);

    while (cond) {
        ph_actor_t *worker = ph_queue_pop(&queue);
        ph_hashtable_t *ht = &worker->supervision->workers;

        for (int i = 0; i < ht->size; ++i) {
            ph_bucket_t *b = ht->values + i;

            if (b->value) {
                ph_actor_t *actor = b->value;

                if (actor->tree_number == bad_tree_number) {
                    cond = 0;
                    break;
                }

                if (actor->supervision) {
                    ph_queue_push(&queue, actor);
                }
            }
        }

        if (!ph_queue_size(&queue)) {
            break;
        }
    }

    ph_queue_destroy(&queue);

    return !cond;
}

void ph_supervisor_add_worker(ph_actor_t *supervisor, ph_actor_t *worker)
{
    pthread_mutex_lock(&supervisor->lock);
    ph_hashtable_insert_ind(&supervisor->supervision->workers, (long)worker, worker);
    pthread_mutex_unlock(&supervisor->lock);

    int new_tree_number = supervisor->supervision->tree_number;

    pthread_mutex_lock(&worker->lock);
    worker->supervisor = supervisor;
    worker->tree_number = new_tree_number;

    if (worker->supervision) {
        ph_queue_t queue;

        ph_queue_init(&queue, dummy_queue_dtor);
        ph_queue_push(&queue, worker);

        worker->supervision->tree_number = new_tree_number;

        while (ph_queue_size(&queue)) {
            ph_actor_t *worker = ph_queue_pop(&queue);
            ph_hashtable_t *ht = &worker->supervision->workers;

            for (int i = 0; i < ht->size; ++i) {
                ph_bucket_t *b = ht->values + i;

                if (b->value) {
                    ph_actor_t *actor = b->value;

                    actor->tree_number = new_tree_number;

                    if (actor->supervision) {
                        actor->supervision->tree_number = new_tree_number;
                        ph_queue_push(&queue, actor);
                    }
                }
            }
        }

        ph_queue_destroy(&queue);
    }

    pthread_mutex_unlock(&worker->lock);
}

ZEND_BEGIN_ARG_INFO_EX(Supervisor___construct_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, supervisor)
    ZEND_ARG_INFO(0, supervisionStrategy)
ZEND_END_ARG_INFO()

PHP_METHOD(Supervisor, __construct)
{
    zval *supervisor;
    zend_long supervision_strategy = PH_SUPERVISOR_ONE_FOR_ONE;
    char supervisor_using_actor_name;
    ph_string_t supervisor_name;

    ZEND_PARSE_PARAMETERS_START(1, -1)
        Z_PARAM_ZVAL(supervisor)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(supervision_strategy)
    ZEND_PARSE_PARAMETERS_END();

    if (!ph_valid_actor_arg(supervisor, &supervisor_using_actor_name, &supervisor_name)) {
        zend_throw_exception(NULL, "Invalid supervisor actor given", 0);
        return;
    }

    switch (supervision_strategy) {
        case PH_SUPERVISOR_ONE_FOR_ONE:
            break;
        default:
            ph_str_value_free(&supervisor_name);
            zend_throw_exception(NULL, "Invalid supervision strategy given", 0);
            return;
    }

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    ph_actor_t *supervising_actor;

    if (supervisor_using_actor_name) {
        supervising_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_name, &supervisor_name);
    } else {
        supervising_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &supervisor_name);
    }

    if (!supervising_actor) {
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
        ph_str_value_free(&supervisor_name);
        zend_throw_exception(NULL, "Invalid supervisor actor", 0);
        return;
    }

    if (supervising_actor->supervision) {
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
        ph_str_value_free(&supervisor_name);
        zend_throw_exception(NULL, "This actor is already a supervisor", 0);
        return;
    }

    ph_supervision_tree_create(supervising_actor, supervision_strategy);

    ph_supervisor_t *supervisor_obj = ph_supervisor_fetch_from_object(Z_OBJ(EX(This)));

    ph_str_set(&supervisor_obj->ref, PH_STRV_P(supervising_actor->ref), PH_STRL_P(supervising_actor->ref));

    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    ph_str_value_free(&supervisor_name);
}

ZEND_BEGIN_ARG_INFO_EX(Supervisor_add_worker_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, worker)
ZEND_END_ARG_INFO()

PHP_METHOD(Supervisor, addWorker)
{
    zval *zworker;
    char worker_using_actor_name;
    ph_string_t worker_name;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(zworker)
    ZEND_PARSE_PARAMETERS_END();

    if (!ph_valid_actor_arg(zworker, &worker_using_actor_name, &worker_name)) {
        zend_throw_exception(NULL, "Invalid worker actor given", 0);
        return;
    }

    ph_supervisor_t *supervisor_obj = ph_supervisor_fetch_from_object(Z_OBJ(EX(This)));
    ph_actor_t *worker, *supervisor;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    supervisor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &supervisor_obj->ref);

    if (!supervisor) { // supervisor has died already - abort (silently)
        // @todo logging?
        ph_str_value_free(&worker_name);
        return;
    }

    if (worker_using_actor_name) {
        worker = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_name, &worker_name);
    } else {
        worker = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &worker_name);
    }

    if (!worker) {
        zend_throw_exception(NULL, "Invalid worker actor given", 0);
    } else if (worker->tree_number != -1) {
        zend_throw_exception(NULL, "This actor is already being supervised", 0);
    } else if (ph_supervisor_cycle_detection(supervisor, worker)) {
        zend_throw_exception(NULL, "A cycle has been detected in the supervision tree", 0);
    } else {
        ph_supervisor_add_worker(supervisor, worker);
    }

    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    ph_str_value_free(&worker_name);
}

ZEND_BEGIN_ARG_INFO_EX(Supervisor_new_worker_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, supervisor)
    ZEND_ARG_INFO(0, supervisionStrategy)
    ZEND_ARG_INFO(0, workers)
ZEND_END_ARG_INFO()

PHP_METHOD(Supervisor, newWorker)
{
    zend_class_entry *actor_class = ph_Actor_ce;
    zval *ctor_args = NULL;
    zend_string *actor_name = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_CLASS(actor_class)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY(ctor_args)
        Z_PARAM_STR(actor_name)
    ZEND_PARSE_PARAMETERS_END();

    ph_supervisor_t *supervisor = ph_supervisor_fetch_from_object(Z_OBJ(EX(This)));
    zval zobj;

    if (object_init_ex(&zobj, ph_ActorRef_ce) != SUCCESS) {
        zend_throw_exception(zend_ce_exception, "Failed to create an ActorRef object from the given Actor class", 0);
    } else {
        ph_actor_ref_create(&zobj, actor_class->name, ctor_args, actor_name, &supervisor->ref);
        RETVAL_OBJ(Z_OBJ(zobj));
    }
}

zend_function_entry Supervisor_methods[] = {
    PHP_ME(Supervisor, __construct, Supervisor___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Supervisor, newWorker, Supervisor_new_worker_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Supervisor, addWorker, Supervisor_add_worker_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_object *ph_supervisor_ctor(zend_class_entry *entry)
{
    ph_supervisor_t *supervisor = ecalloc(1, sizeof(ph_supervisor_t) + zend_object_properties_size(entry));

    zend_object_std_init(&supervisor->obj, entry);
    object_properties_init(&supervisor->obj, entry);

    supervisor->obj.handlers = &ph_Supervisor_handlers;

    return &supervisor->obj;
}

void ph_supervisor_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\Supervisor", Supervisor_methods);
    ph_Supervisor_ce = zend_register_internal_class(&ce);
    ph_Supervisor_ce->ce_flags |= ZEND_ACC_FINAL;
    ph_Supervisor_ce->create_object = ph_supervisor_ctor;

    memcpy(&ph_Supervisor_handlers, zh, sizeof(zend_object_handlers));

    ph_Supervisor_handlers.offset = XtOffsetOf(ph_supervisor_t, obj);

    zend_declare_class_constant_long(ph_Supervisor_ce,  ZEND_STRL("ONE_FOR_ONE"), PH_SUPERVISOR_ONE_FOR_ONE);
}
