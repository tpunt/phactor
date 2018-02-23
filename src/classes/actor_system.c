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
__thread ph_task_t *currently_processing_task;
__thread ph_thread_t *thread;
__thread int thread_offset;
ph_thread_t main_thread;

zend_object_handlers phactor_actor_system_handlers;
zend_class_entry *ActorSystem_ce;

void send_local_message(ph_actor_t *to_actor, ph_task_t *task)
{
    ph_message_t *message = ph_msg_create(&task->u.smt.from_actor_ref, task->u.smt.message);

    assert((long)ph_actor_retrieve_from_ref(&task->u.smt.from_actor_ref) & -2);

    pthread_mutex_lock(&to_actor->lock);
    ph_queue_push(&to_actor->mailbox, message);

    if (to_actor->state != PH_ACTOR_ACTIVE && ph_queue_size(&to_actor->mailbox) == 1) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + to_actor->thread_offset;

        pthread_mutex_lock(&thread->tasks.lock);
        if (to_actor->state == PH_ACTOR_NEW) {
            ph_queue_push(&thread->tasks, ph_task_create_process_message(to_actor));
        } else {
            ph_queue_push(&thread->tasks, ph_task_create_resume_actor(to_actor));
        }
        pthread_mutex_unlock(&thread->tasks.lock);
    }
    pthread_mutex_unlock(&to_actor->lock);
}

void send_remote_message(ph_task_t *task)
{
    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    if (PHACTOR_G(actor_system)->shutdown) {
        // The actor system was shut down and the actor being sent to was freed.
        // This will need to be changed in future.
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);
        return;
    }
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    // @todo debugging purposes only - no implementation yet
    printf("Tried to send a message to a non-existent (or remote) actor\n");
    assert(0);
}

zend_bool send_message(ph_task_t *task)
{
    ph_actor_t *to_actor = ph_actor_retrieve_from_name(&task->u.smt.to_actor_name);
    zend_bool sent = 1;

    if (to_actor) {
        if ((ulong)to_actor == 1) { // @todo how to prevent infinite loop if actor creation fails?
            ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count;

            pthread_mutex_lock(&thread->tasks.lock);
            ph_queue_push(&thread->tasks, task);
            pthread_mutex_unlock(&thread->tasks.lock);
            sent = 0;
        } else {
            send_local_message(to_actor, task);
        }
    } else {
        // this is a temporary hack - in future, if we would like the ability to
        // send to either a name or ref, then wrapper objects (ActorRef and
        // ActorName) should be used instead
        to_actor = ph_actor_retrieve_from_ref(&task->u.smt.to_actor_name);

        if ((ulong)to_actor == 1) { // @todo how to prevent infinite loop if actor creation fails?
            ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count;

            pthread_mutex_lock(&thread->tasks.lock);
            ph_queue_push(&thread->tasks, task);
            pthread_mutex_unlock(&thread->tasks.lock);
            sent = 0;
        } else {
            if (to_actor) {
                send_local_message(to_actor, task);
            } else {
                send_remote_message(task);
            }
        }
    }

    return sent;
}

void process_message(ph_actor_t *for_actor)
{
    ph_vmcontext_set(&for_actor->context.vmc);
    // swap into process_message_handler
#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &for_actor->context.mc);
#else
    ph_mcontext_start(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, for_actor->context.mc.cb);
    // ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &for_actor->context.mc, 0);
#endif
}

void resume_actor(ph_actor_t *actor)
{
    ph_vmcontext_set(&actor->context.vmc);
    // swap back into receive_block
#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &actor->context.mc);
#else
    ph_mcontext_resume(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &actor->context.mc);
    // ph_mcontext_swap(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, &actor->context.mc, 2);
#endif
}

void new_actor(ph_task_t *task)
{
    ph_string_t class_name = task->u.nat.class_name;
    zend_string *class = zend_string_init(PH_STRV(class_name), PH_STRL(class_name), 0);
    zend_class_entry *ce = zend_fetch_class_by_name(class, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
    ph_entry_t *args = task->u.nat.args;
    int argc = task->u.nat.argc;
    zend_function *constructor;
    zval zobj;

    PHACTOR_ZG(allowed_to_construct_object) = 1;

    if (object_init_ex(&zobj, ce) != SUCCESS) {
        // @todo this will throw an exception in the new thread, rather than at
        // the call site. This doesn't even have an execution context - how
        // should it behave?
        zend_throw_exception_ex(zend_ce_exception, 0, "Failed to create an actor from class '%s'\n", ZSTR_VAL(class));
        zend_string_free(class);
        PHACTOR_ZG(allowed_to_construct_object) = 0;
        return;
    }

    PHACTOR_ZG(allowed_to_construct_object) = 0;

    ph_actor_t *new_actor = (ph_actor_t *)((char *)Z_OBJ(zobj) - Z_OBJ(zobj)->handlers->offset);
    ph_string_t *named_actor_key = task->u.nat.named_actor_key;

    // @todo what are the technical reasons for not doing this in phactor_actor_ctor?
    zend_vm_stack_init();
    ph_vmcontext_get(&new_actor->context.vmc);

    pthread_mutex_lock(&PHACTOR_G(actor_system)->named_actors.lock);
    ph_named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, named_actor_key);

    if (!named_actor) { // quite unexpected
        // Occurs if an actor is spawn()ed and then remove()d before the new
        // thread has a chance to create it.
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->named_actors.lock);
        ph_actor_free(new_actor);
        zend_string_free(class);
        return;
    }

    pthread_mutex_lock(&named_actor->actors.lock);
    if (named_actor->actors.used == named_actor->perceived_used) { // quite unexpected
        // Occurs if multiple actors are spawn()ed, and then multiple actors
        // (but not all of them) are remove()d before they have all been
        // created. This leaves more new actor tasks that have still yet to be
        // created than the perceived_used count of a named actor. So we have to
        // discard them.
        pthread_mutex_unlock(&named_actor->actors.lock);
        pthread_mutex_unlock(&PHACTOR_G(actor_system)->named_actors.lock);
        ph_actor_free(new_actor);
        zend_string_free(class);
        return;
    }

    new_actor->name = named_actor_key;
    pthread_mutex_unlock(&named_actor->actors.lock);

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors.lock);
    ph_hashtable_insert(&PHACTOR_G(actor_system)->actors, &new_actor->ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors.lock);

    pthread_mutex_lock(&named_actor->actors.lock);
    ph_hashtable_insert(&named_actor->actors, &new_actor->ref, new_actor);

    if (named_actor->state == PH_NAMED_ACTOR_CONSTRUCTING) {
        named_actor->state = PH_NAMED_ACTOR_ACTIVE;
    }
    pthread_mutex_unlock(&named_actor->actors.lock);

    pthread_mutex_unlock(&PHACTOR_G(actor_system)->named_actors.lock);

    constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

    if (constructor) {
        int result;
        zval retval, zargs[argc];
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
        ZVAL_STRINGL(&fci.function_name, "__construct", sizeof("__construct") - 1);

        result = zend_call_function(&fci, NULL);

        if (result == FAILURE) {
            if (!EG(exception)) {
                // same as problem above?
                zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(class), "::", "__construct");
                zend_string_free(class);
                return;
            }
        }

        zval_dtor(&fci.function_name);
        zval_ptr_dtor(&retval);
    }

    zend_string_free(class);
}

void perform_actor_removals(void)
{
    ph_vector_t *actor_removals = PHACTOR_G(actor_system)->actor_removals + thread_offset;

    pthread_mutex_lock(&actor_removals->lock);

    while (ph_vector_size(actor_removals)) {
        ph_actor_remove(ph_vector_pop(actor_removals));
    }

    pthread_mutex_unlock(&actor_removals->lock);
}

static void handle_actor_next_action(ph_actor_t *actor)
{
    pthread_mutex_lock(&actor->lock);
    if (actor->state == PH_ACTOR_ACTIVE) {
        // Actor has finished executing (if it became blocked, then
        // its state would currently be idle). Its state now needs
        // to be updated (to new), and if it has any messages in its
        // mailbox, then it needs to be rescheduled.
        ph_mcontext_reset(&actor->context.mc);

        if (ph_queue_size(&actor->mailbox)) {
            ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + actor->thread_offset;
            ph_task_t *task = ph_task_create_process_message(actor);

            pthread_mutex_lock(&thread->tasks.lock);
            ph_queue_push(&thread->tasks, task);
            pthread_mutex_unlock(&thread->tasks.lock);
        }

        actor->state = PH_ACTOR_NEW;
    }
    pthread_mutex_unlock(&actor->lock);
}

void message_handling_loop(void)
{
    while (1) {
        perform_actor_removals();

        pthread_mutex_lock(&PH_THREAD_G(tasks).lock);
        ph_task_t *current_task = ph_queue_pop(&PH_THREAD_G(tasks));
        pthread_mutex_unlock(&PH_THREAD_G(tasks).lock);

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
                if (!send_message(current_task)) {
                    // skip the freeing of the current task if the message was never sent
                    // this can occur if an actor is still being spawned
                    continue;
                }
                break;
            case PH_PROCESS_MESSAGE_TASK:
                currently_processing_task = current_task; // tls for the currently processing actor
                ph_actor_t *for_actor = current_task->u.pmt.for_actor;

                process_message(for_actor);
                handle_actor_next_action(for_actor);
                break;
            case PH_RESUME_ACTOR_TASK:
                pthread_mutex_lock(&current_task->u.rat.actor->lock);
                ph_actor_t *actor = current_task->u.rat.actor;
                actor->state = PH_ACTOR_ACTIVE;
                pthread_mutex_unlock(&current_task->u.rat.actor->lock);

                resume_actor(actor);
                handle_actor_next_action(actor);
                break;
            case PH_NEW_ACTOR_TASK:
                new_actor(current_task);
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

    message_handling_loop();

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
        pthread_mutex_init(&thread->ph_task_mutex, NULL);
        ph_queue_init(&thread->tasks, ph_task_free);
        ph_vector_init(PHACTOR_G(actor_system)->actor_removals + i, 4, ph_actor_remove);

        if (i != PHACTOR_G(actor_system)->thread_count) {
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

    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    if (!PHACTOR_G(actor_system)->shutdown) {
        PHACTOR_G(actor_system)->shutdown = !PHACTOR_G(actor_system)->daemonised;
    }
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    // @todo use own specialised loop here? Only messages should need to be
    // handled in the main thread (for now)
    message_handling_loop();

    while (PHACTOR_G(actor_system)->thread_count != PHACTOR_G(actor_system)->finished_thread_count);

    ph_hashtable_destroy(&PHACTOR_G(actor_system)->named_actors, ph_named_actor_remove);

    // perform_actor_removals(); // shouldn't be needed

    pthread_mutex_lock(&PHACTOR_G(actor_system)->lock);
    ++PHACTOR_G(actor_system)->finished_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->lock);

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;

        pthread_mutex_destroy(&thread->ph_task_mutex);
        ph_queue_destroy(&thread->tasks);
        pthread_join(thread->pthread, NULL);
    }

    PHACTOR_G(actor_system)->initialised = 0;
}

static zend_object* phactor_actor_system_ctor(zend_class_entry *entry)
{
    if (!PHACTOR_G(actor_system)) {
        PHACTOR_G(actor_system) = ecalloc(1, sizeof(ph_actor_system_t) + zend_object_properties_size(entry));

        // @todo create the UUID on actor creation - this is needed for remote actor systems only

        ph_hashtable_init(&PHACTOR_G(actor_system)->actors, 8);
        ph_hashtable_init(&PHACTOR_G(actor_system)->named_actors, 8);

        zend_object_std_init(&PHACTOR_G(actor_system)->obj, entry);
        object_properties_init(&PHACTOR_G(actor_system)->obj, entry);

        PHACTOR_G(actor_system)->obj.handlers = &phactor_actor_system_handlers;
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
    ZEND_ASSERT(!PHACTOR_G(actor_system)->actors.used);

    for (int i = 0; i <= PHACTOR_G(actor_system)->thread_count; ++i) {
        ph_vector_destroy(PHACTOR_G(actor_system)->actor_removals + i);
    }

    pthread_mutex_destroy(&PHACTOR_G(actor_system)->lock);
    pthread_mutex_destroy(&PHACTOR_G(actor_system)->worker_threads[PHACTOR_G(actor_system)->thread_count].ph_task_mutex);

    free(PHACTOR_G(actor_system)->worker_threads);
    free(PHACTOR_G(actor_system)->actor_removals);
    ph_hashtable_destroy(&PHACTOR_G(actor_system)->actors, ph_actor_free_dummy);
    // PHACTOR_G(actor_system)->named_actors is already destroyed
}

ZEND_BEGIN_ARG_INFO(ActorSystem_construct_arginfo, 0)
    ZEND_ARG_INFO(0, daemonised)
    ZEND_ARG_INFO(0, thread_count)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorSystem, __construct)
{
    zend_long thread_count = PHACTOR_G(actor_system)->thread_count;

    ZEND_PARSE_PARAMETERS_START(0, 2)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(PHACTOR_G(actor_system)->daemonised)
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

void actor_system_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\ActorSystem", ActorSystem_methods);
    ActorSystem_ce = zend_register_internal_class(&ce);
    ActorSystem_ce->create_object = phactor_actor_system_ctor;
    ActorSystem_ce->ce_flags |= ZEND_ACC_FINAL;
    ActorSystem_ce->serialize = zend_class_serialize_deny;
    ActorSystem_ce->unserialize = zend_class_unserialize_deny;

    memcpy(&phactor_actor_system_handlers, zh, sizeof(zend_object_handlers));

    phactor_actor_system_handlers.offset = XtOffsetOf(ph_actor_system_t, obj);
    phactor_actor_system_handlers.dtor_obj = php_actor_system_dtor_object;
    phactor_actor_system_handlers.free_obj = php_actor_system_free_object;
}
