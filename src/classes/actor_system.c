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
#include <main/php_main.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_interfaces.h>
#include <main/SAPI.h>
#include <ext/standard/basic_functions.h>

#include "php_phactor.h"
#include "src/ph_task.h"
#include "src/ph_copy.h"
#include "src/ds/ph_queue.h"
#include "src/ph_debug.h"
#include "src/ph_message.h"
#include "src/ph_context.h"
#include "src/ds/ph_hashtable.h"
#include "src/classes/actor_system.h"

ph_actor_system_t *actor_system;
__thread ph_actor_t *currently_processing_actor;
__thread ph_thread_t *thread;
__thread int thread_offset;
ph_thread_t main_thread;

zend_object_handlers ph_ActorSystem_handlers;
zend_class_entry *ph_ActorSystem_ce;

void send_local_message(ph_actor_t *to_actor, ph_task_t *task)
{
    ph_message_t *message = ph_msg_create(&task->u.smt.from_actor_ref, task->u.smt.message);

    pthread_mutex_lock(&to_actor->lock);
    ph_queue_push(&to_actor->mailbox, message);

    if (to_actor->state == PH_ACTOR_IDLE && ph_queue_size(&to_actor->mailbox) == 1) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + to_actor->thread_offset;

        pthread_mutex_lock(&thread->tasks.lock);
        ph_queue_push(&thread->tasks, ph_task_create_resume_actor(to_actor));
        pthread_mutex_unlock(&thread->tasks.lock);
    }
    pthread_mutex_unlock(&to_actor->lock);
}

void send_message(ph_task_t *task)
{
    ph_actor_t *actor;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    if (task->u.smt.using_actor_name) {
        actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_name, &task->u.smt.to_actor_name);
    } else {
        actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &task->u.smt.to_actor_name);
    }

    if (actor) {
        send_local_message(actor, task);
    } else {
        // Actor either didn't exist or no longer exists
        // @todo Perhaps implement logging or something for this scenario?
        // How to see if this is a remote actor? This is where ActorRef
        // objects would come into play
    }
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
}

void process_message(ph_actor_t *for_actor)
{
    ph_vmcontext_set(&for_actor->internal->context.vmc);
    // swap into process_message_handler
#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &for_actor->internal->context.mc);
#else
    ph_mcontext_start(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, for_actor->internal->context.mc.cb);
#endif
}

void resume_actor(ph_actor_t *actor)
{
    ph_vmcontext_set(&actor->internal->context.vmc);
    // swap back into receive_block
#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &actor->internal->context.mc);
#else
    ph_mcontext_resume(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &actor->internal->context.mc);
#endif
}

static zend_execute_data dummy_execute_data;

ph_actor_t *new_actor(ph_task_t *task)
{
    ph_string_t actor_class = task->u.nat.actor_class;
    zend_string *class = zend_string_init(PH_STRV(actor_class), PH_STRL(actor_class), 0);
    zend_class_entry *ce = zend_fetch_class_by_name(class, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
    zval zobj;

    PHACTOR_ZG(allowed_to_construct_object) = 1;

    if (object_init_ex(&zobj, ce) != SUCCESS) {
        // @todo this will throw an exception in the new thread, rather than at
        // the call site. This doesn't even have an execution context - how
        // should it behave?
        zend_throw_exception_ex(NULL, 0, "Failed to create an actor from class '%s'\n", ZSTR_VAL(class));
        zend_string_free(class);
        PHACTOR_ZG(allowed_to_construct_object) = 0;
        return NULL;
    }

    PHACTOR_ZG(allowed_to_construct_object) = 0;

    ph_actor_internal_t *actor_internal = ph_actor_internal_retrieve_from_object(Z_OBJ(zobj));
    ph_string_t *actor_ref = &task->u.nat.actor_ref;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    ph_actor_t *new_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, actor_ref);

    if (!new_actor) {
        // This branch can be hit when a supervisor dies, where it will kill all
        // of its workers (this actor being a worker). Just abort it (silently)
        // @todo log this?
        zend_string_free(class);
        return NULL;
    }

    actor_internal->ref = new_actor->ref;

    pthread_mutex_lock(&new_actor->lock);
    new_actor->internal = actor_internal;
    pthread_mutex_unlock(&new_actor->lock);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    zend_function *constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));
    ph_entry_t *args = new_actor->ctor_args;
    int argc = new_actor->ctor_argc;

    if (constructor) {
        int result;
        zval retval, zargs[argc]; // @todo VLAs are not supported by MSVC compiler
        zend_fcall_info fci;

        for (int i = 0; i < argc; ++i) {
            ph_entry_convert_to_zval(zargs + i, args + i);
        }

        fci.size = sizeof(fci);
        fci.object = Z_OBJ(zobj);
        fci.retval = &retval;
        fci.param_count = argc;
        fci.params = zargs;
        fci.no_separation = 1;
        ZVAL_INTERNED_STR(&fci.function_name, common_strings.__construct);

        EG(current_execute_data) = &dummy_execute_data;

        result = zend_call_function(&fci, NULL);

        EG(current_execute_data) = NULL;

        if (result == FAILURE && !EG(exception)) {
            // same as problem above?
            zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(class), "::", "__construct");
            zend_string_free(class);
            return NULL;
        }

        if (EG(exception)) {
            ph_actor_crash(new_actor);
            zend_clear_exception();
            new_actor = NULL;
        } else {
            zval_ptr_dtor(&retval);
        }
    }

    zend_string_free(class);

    // @todo if a special state was given to restarting actors (such as
    // PH_ACTOR_RESTARTING), then the hash table traversal could be avoided
    if (new_actor && new_actor->supervision) {
        ph_hashtable_apply(&new_actor->supervision->workers, ph_supervisor_one_for_one);
    }

    return new_actor;
}

void perform_actor_removals(void)
{
    ph_vector_t *actor_removals = PHACTOR_G(actor_system)->actor_removals + thread_offset;

    pthread_mutex_lock(&actor_removals->lock);

    while (ph_vector_size(actor_removals)) {
        ph_actor_free(ph_vector_pop(actor_removals));
    }

    pthread_mutex_unlock(&actor_removals->lock);
}

void message_handling_loop(ph_thread_t *ph_thread)
{
    while (1) {
        perform_actor_removals();

        pthread_mutex_lock(&ph_thread->tasks.lock);
        ph_task_t *current_task = ph_queue_pop(&ph_thread->tasks);
        pthread_mutex_unlock(&ph_thread->tasks.lock);

        if (!current_task) {
            pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
            if (PHACTOR_G(actor_system)->shutdown) {
                pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);
                break;
            }
            pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);
            continue;
        }

        switch (current_task->type) {
            case PH_SEND_MESSAGE_TASK:
                send_message(current_task);
                ph_str_value_free(&current_task->u.smt.to_actor_name);
                break;
            case PH_RESUME_ACTOR_TASK:
                pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
                ph_actor_t *actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &current_task->u.rat.actor_ref);
                pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

                assert(actor && actor->internal); // may change in future

                ph_str_value_free(&current_task->u.rat.actor_ref);

                resume_actor(actor);
                break;
            case PH_NEW_ACTOR_TASK:
                currently_processing_actor = new_actor(current_task);

                if (currently_processing_actor) {
                    process_message(currently_processing_actor);
                }
        }

        free(current_task);
    }
}

void *worker_function(ph_thread_t *ph_thread)
{
    thread_offset = ph_thread->offset;
    thread = ph_thread;
    ph_thread->id = (ulong) pthread_self();
    ph_thread->ls = ts_resource(0);

    TSRMLS_CACHE_UPDATE();

    SG(server_context) = PHACTOR_SG(PHACTOR_G(main_thread).ls, server_context);
    SG(sapi_started) = 0;

    PG(expose_php) = 0;
    PG(auto_globals_jit) = 0;

    php_request_startup();
    copy_execution_context();

    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    ++PHACTOR_G(actor_system)->prepared_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    ph_vmcontext_get(&ph_thread->context.vmc);

    message_handling_loop(ph_thread);

    PG(report_memleaks) = 0;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    ++PHACTOR_G(actor_system)->finished_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    // Block here to prevent premature freeing of actors when the could be being
    // used by other threads
    while (PHACTOR_G(actor_system)->thread_count + 1 != PHACTOR_G(actor_system)->finished_thread_count);

    perform_actor_removals();

    ph_vmcontext_set(&ph_thread->context.vmc);

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

/* Take from ext/standard/basic_functions.c */
void user_shutdown_function_dtor(zval *zv)
{
    int i;
    php_shutdown_function_entry *shutdown_function_entry = Z_PTR_P(zv);

    for (i = 0; i < shutdown_function_entry->arg_count; i++) {
        zval_ptr_dtor(&shutdown_function_entry->arguments[i]);
    }

    efree(shutdown_function_entry->arguments);
    efree(shutdown_function_entry);
}

void initialise_actor_system(zend_long thread_count)
{
    PHACTOR_G(actor_system)->thread_count = thread_count;
    PHACTOR_G(main_thread).id = (ulong) pthread_self();
    PHACTOR_G(main_thread).ls = TSRMLS_CACHE;
    PHACTOR_G(actor_system)->actor_removals = calloc(sizeof(ph_vector_t), PHACTOR_G(actor_system)->thread_count + 1);
    PHACTOR_G(actor_system)->worker_threads = calloc(sizeof(ph_thread_t), PHACTOR_G(actor_system)->thread_count + 1);
    pthread_mutex_init(&PHACTOR_G(actor_system)->lock, NULL);

    for (int i = 0; i <= PHACTOR_G(actor_system)->thread_count; ++i) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;

        thread->offset = i;
        ph_queue_init(&thread->tasks, ph_task_free);

        if (i != PHACTOR_G(actor_system)->thread_count) {
            ph_vector_init(PHACTOR_G(actor_system)->actor_removals + i, 4, ph_actor_free);
            pthread_create(&thread->pthread, NULL, (void *) worker_function, thread);
        }
    }

    thread_offset = PHACTOR_G(actor_system)->thread_count;
    thread = PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count;

    while (PHACTOR_G(actor_system)->thread_count != PHACTOR_G(actor_system)->prepared_thread_count);

    // automatically invoke ActorSystem::block(), using [&PHACTOR_G(actor_system)->obj, "block"] callable
    php_shutdown_function_entry shutdown_function_entry;
    zval zcallable, zobj, zmethod;

    shutdown_function_entry.arguments = emalloc(sizeof(zval));
    shutdown_function_entry.arg_count = 1;

    array_init_size(shutdown_function_entry.arguments, 2);
    ZVAL_OBJ(&zobj, &PHACTOR_G(actor_system)->obj);
    ZVAL_STR(&zmethod, zend_string_init(ZEND_STRL("block"), 0));
    zend_hash_index_add_new(Z_ARR(shutdown_function_entry.arguments[0]), 0, &zobj);
    zend_hash_index_add_new(Z_ARR(shutdown_function_entry.arguments[0]), 1, &zmethod);

    if (!BG(user_shutdown_function_names)) {
        ALLOC_HASHTABLE(BG(user_shutdown_function_names));
        zend_hash_init(BG(user_shutdown_function_names), 0, NULL, user_shutdown_function_dtor, 0);
    }

    zend_hash_next_index_insert_mem(BG(user_shutdown_function_names), &shutdown_function_entry, sizeof(php_shutdown_function_entry));

    PHACTOR_G(actor_system)->initialised = 1;
}

void force_shutdown_actor_system()
{
    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    PHACTOR_G(actor_system)->shutdown = 1; // @todo this will not work, since worker threads may still be working
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);
}

void scheduler_blocking()
{
    if (!PHACTOR_G(actor_system)->initialised) {
        return;
    }

    if (EG(exit_status) == 255) {
        for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
            ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;

            pthread_kill(thread->pthread, SIGKILL);
        }
    } else {
        // @todo use own specialised loop here? Only messages should need to be
        // handled in the main thread (for now)
        message_handling_loop(PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count);

        while (PHACTOR_G(actor_system)->thread_count != PHACTOR_G(actor_system)->finished_thread_count);
    }

    ph_hashtable_destroy(&PHACTOR_G(actor_system)->actors_by_name);
    ph_hashtable_destroy(&PHACTOR_G(actor_system)->actors_by_ref);

    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    ++PHACTOR_G(actor_system)->finished_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;

        ph_queue_destroy(&thread->tasks);

        if (EG(exit_status) != 255) {
            pthread_join(thread->pthread, NULL);
        }
    }

    PHACTOR_G(actor_system)->initialised = 0;
}

static zend_object* phactor_actor_system_ctor(zend_class_entry *entry)
{
    if (!PHACTOR_G(actor_system)) {
        PHACTOR_G(actor_system) = ecalloc(1, sizeof(ph_actor_system_t) + zend_object_properties_size(entry));

        // @todo create the UUID on actor creation - this is needed for remote actor systems only

        ph_hashtable_init(&PHACTOR_G(actor_system)->actors_by_ref, 64, ph_actor_mark_for_removal);
        ph_hashtable_init(&PHACTOR_G(actor_system)->actors_by_name, 64, ph_actor_free_dummy);

        zend_object_std_init(&PHACTOR_G(actor_system)->obj, entry);
        object_properties_init(&PHACTOR_G(actor_system)->obj, entry);

        PHACTOR_G(actor_system)->obj.handlers = &ph_ActorSystem_handlers;
        PHACTOR_G(actor_system)->thread_count = sysconf(_SC_NPROCESSORS_ONLN) + ASYNC_THREAD_COUNT;
    }

    return &PHACTOR_G(actor_system)->obj;
}

void php_actor_system_dtor_object(zend_object *obj)
{
    zend_object_std_dtor(obj);
}

void php_actor_system_free_object(zend_object *obj)
{
    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        ph_vector_destroy(PHACTOR_G(actor_system)->actor_removals + i);
    }

    pthread_mutex_destroy(&PHACTOR_G(actor_system)->lock);

    free(PHACTOR_G(actor_system)->worker_threads);
    free(PHACTOR_G(actor_system)->actor_removals);
}

zval *ph_actor_system_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_error(zend_ce_error, "Properties on ActorSystem objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void ph_actor_system_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_error(zend_ce_error, "Properties on ActorSystem objects are not enabled", 0);
}

ZEND_BEGIN_ARG_INFO(ActorSystem_construct_arginfo, 0)
    ZEND_ARG_INFO(0, thread_count)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorSystem, __construct)
{
    zend_long thread_count = PHACTOR_G(actor_system)->thread_count;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(thread_count)
    ZEND_PARSE_PARAMETERS_END();

    if (thread_count < 1 || thread_count > 1024) {
        zend_throw_error(NULL, "Invalid thread count provided (an integer between 1 and 1024 (inclusive) is required)");
        return;
    }

    if (PHACTOR_G(actor_system)->initialised) {
        pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
        PHACTOR_G(actor_system)->shutdown = 1;
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

        // this has to be an E_ERROR, since an exception does not instantly bail out
        zend_error_noreturn(E_ERROR, "The actor system has already been created");
        return;
    }

    initialise_actor_system(thread_count);
}

ZEND_BEGIN_ARG_INFO(ActorSystem_shutdown_arginfo, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorSystem, shutdown)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    force_shutdown_actor_system();
}

ZEND_BEGIN_ARG_INFO(ActorSystem_block_arginfo, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorSystem, block)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    scheduler_blocking();
}

zend_function_entry ActorSystem_methods[] = {
    PHP_ME(ActorSystem, __construct, ActorSystem_construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ActorSystem, shutdown, ActorSystem_shutdown_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(ActorSystem, block, ActorSystem_block_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void ph_actor_system_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\ActorSystem", ActorSystem_methods);
    ph_ActorSystem_ce = zend_register_internal_class(&ce);
    ph_ActorSystem_ce->create_object = phactor_actor_system_ctor;
    ph_ActorSystem_ce->ce_flags |= ZEND_ACC_FINAL;
    ph_ActorSystem_ce->serialize = zend_class_serialize_deny;
    ph_ActorSystem_ce->unserialize = zend_class_unserialize_deny;

    memcpy(&ph_ActorSystem_handlers, zh, sizeof(zend_object_handlers));

    ph_ActorSystem_handlers.offset = XtOffsetOf(ph_actor_system_t, obj);
    ph_ActorSystem_handlers.dtor_obj = php_actor_system_dtor_object;
    ph_ActorSystem_handlers.free_obj = php_actor_system_free_object;
    ph_ActorSystem_handlers.read_property = ph_actor_system_read_property;
    ph_ActorSystem_handlers.write_property = ph_actor_system_write_property;
}
