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

#include "php_phactor.h"
#include "ph_copy.h"
#include "ph_hashtable.h"
#include "ph_debug.h"

#ifndef ZTS
# error "Zend Thread Safetfy (ZTS) mode is required"
#endif

#if !defined(ZEND_ENABLE_STATIC_TSRMLS_CACHE) || !ZEND_ENABLE_STATIC_TSRMLS_CACHE
# error "TSRMLS static cache is required"
#endif

ZEND_DECLARE_MODULE_GLOBALS(phactor)

void *scheduler();
void process_message(/*task_t *task*/);
void resume_actor(actor_t *actor);
void enqueue_task(task_t *task, int thread_offset);
task_t *dequeue_task(void);
void call_receive_method(zend_object *object, zval *retval_ptr, zval *from_actor, zval *message);
actor_t *get_actor_from_name(ph_string_t *actor_ref);
actor_t *get_actor_from_ref(ph_string_t *actor_ref);
actor_t *get_actor_from_object(zend_object *actor_obj);
actor_t *get_actor_from_zval(zval *actor_zval_obj);
task_t *create_send_message_task(ph_string_t *from_actor_ref, ph_string_t *to_actor_name, zval *message);
task_t *create_process_message_task(actor_t *for_actor);
task_t *create_resume_actor_task(actor_t *actor);
task_t *create_new_actor_task(ph_string_t *named_actor_key, zend_string *class_name, zval *args, int argc);
zend_object* phactor_actor_ctor(zend_class_entry *entry);
void delete_named_actor(void *named_actor_void);
void delete_actor(void *actor);
void new_actor(task_t *task);
message_t *create_new_message(ph_string_t *from_actor_ref, entry_t *message);
void send_message(task_t *task);
void send_local_message(actor_t *to_actor, task_t *task);
void send_remote_message(task_t *task);
void initialise_actor_system(void);
void perform_actor_removals(void);
void mark_actor_for_removal(actor_t *actor);

#define ACTOR_REF_LEN 33

static __thread int thread_offset;
static __thread task_t *currently_processing_task;
static __thread thread_t *thread;
static __thread int allowed_to_construct_object;

thread_t main_thread;
pthread_mutex_t phactor_mutex;
pthread_mutex_t phactor_actors_mutex;
pthread_mutex_t phactor_named_actors_mutex;
pthread_mutex_t actor_removal_mutex;
pthread_mutex_t global_actor_id_lock;
actor_system_t *actor_system;
int php_shutdown = 0;
int global_actor_id = 0;
zend_object_handlers phactor_actor_handlers;
zend_object_handlers phactor_actor_system_handlers;
void ***phactor_instance = NULL;

zend_class_entry *ActorSystem_ce;
zend_class_entry *Actor_ce;

void save_executor_globals(zend_executor_globals *eg)
{
    // eg->current_execute_data = EG(current_execute_data);
    *eg = *TSRMG_BULK_STATIC(executor_globals_id, zend_executor_globals *);
    // eg->vm_stack_top = EG(vm_stack_top);
    // eg->vm_stack_end = EG(vm_stack_end);
    // eg->vm_stack = EG(vm_stack);
    // eg->fake_scope = EG(fake_scope);
    //
}

void restore_executor_globals(zend_executor_globals *eg)
{
    zend_executor_globals *ceg = TSRMG_BULK_STATIC(executor_globals_id, zend_executor_globals *);

    EG(current_execute_data) = eg->current_execute_data;
    // EG(vm_stack_top) = eg->vm_stack_top;
    // EG(vm_stack_end) = eg->vm_stack_end;
    // EG(vm_stack) = eg->vm_stack;
    // EG(fake_scope) = eg->fake_scope;
    //
}

void message_handling_loop(void)
{
    while (1) {
        perform_actor_removals();

        task_t *current_task = dequeue_task();

        if (!current_task) {
            pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
            if (PHACTOR_G(php_shutdown)) {
                pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));
                break;
            }
            pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));
            continue;
        }

        switch (current_task->task_type) {
            case SEND_MESSAGE_TASK:
                send_message(current_task);
                break;
            case PROCESS_MESSAGE_TASK:
                currently_processing_task = current_task; // tls for the currently processing actor
                actor_t *for_actor = current_task->task.pmt.for_actor;
                // swap into process_message
                ph_swap_context(&PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context, &for_actor->actor_context);

                pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
                if (for_actor->state == ACTIVE_STATE) { // update actor state if finished
                    // hit if actor did not block at all during execution
                    ph_reset_context(&for_actor->actor_context);
                    if (for_actor->mailbox) {
                        enqueue_task(create_process_message_task(for_actor), for_actor->thread_offset);
                    }
                    for_actor->state = NEW_STATE;
                }
                pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
                break;
            case RESUME_ACTOR_TASK:
                pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
                actor_t *actor = current_task->task.rat.actor;
                actor->state = ACTIVE_STATE;
                pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
                resume_actor(actor);

                pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
                if (actor->state == ACTIVE_STATE) { // update actor state if finished
                    // hit if actor was blocking at all during execution
                    ph_reset_context(&actor->actor_context);
                    if (actor->mailbox) {
                        enqueue_task(create_process_message_task(actor), actor->thread_offset);
                    }
                    actor->state = NEW_STATE;
                }
                pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
                break;
            case NEW_ACTOR_TASK:
                new_actor(current_task);
        }

        free(current_task);
    }
}

void *worker_function(thread_t *ph_thread)
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

    pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
    ++PHACTOR_G(actor_system)->prepared_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));

    save_executor_globals(&ph_thread->eg);

    message_handling_loop();

    PG(report_memleaks) = 0;

    // Block here to prevent premature freeing of actors when the could be being
    // used by other threads
    pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
    ++PHACTOR_G(actor_system)->finished_thread_count;
    pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));

    while (PHACTOR_G(actor_system)->thread_count != PHACTOR_G(actor_system)->finished_thread_count);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    ph_hashtable_delete_by_value(&PHACTOR_G(actor_system)->actors, delete_actor, actor_t *, thread_offset, thread_offset);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

void process_message(/*task_t *task*/)
{
    task_t *task = currently_processing_task;
    actor_t *for_actor = task->task.pmt.for_actor;
    zval return_value, from_actor_zval, message_zval;

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    message_t *message = for_actor->mailbox;
    for_actor->mailbox = for_actor->mailbox->next_message;
    for_actor->state = ACTIVE_STATE;
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ZVAL_STR(&from_actor_zval, zend_string_init(PH_STRV(message->from_actor_ref), PH_STRL(message->from_actor_ref), 0));
    ph_convert_entry_to_zval(&message_zval, message->message);

    call_receive_method(&for_actor->obj, &return_value, &from_actor_zval, &message_zval);

    free(PH_STRV(message->from_actor_ref));
    zval_ptr_dtor(&message_zval);
    ph_entry_delete(message->message);
    free(message);

    zval_ptr_dtor(&from_actor_zval);
    zval_ptr_dtor(&return_value);

    ph_set_context(&PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context);
}

void send_message(task_t *task)
{
    actor_t *to_actor = get_actor_from_name(&task->task.smt.to_actor_name);

    if (to_actor) {
        if ((ulong)to_actor == 1) {
            // @todo how to prevent infinite loop if actor creation fails?
            enqueue_task(task, PHACTOR_G(actor_system)->thread_count);
        } else {
            send_local_message(to_actor, task);
        }
    } else {
        // this is a temporary hack - in future, if we would like the ability to
        // send to either a name or ref, then wrapper objects (ActorRef and
        // ActorName) should be used instead
        to_actor = get_actor_from_ref(&task->task.smt.to_actor_name);

        if (to_actor) {
            send_local_message(to_actor, task);
        } else {
            send_remote_message(task);
        }
    }
}

void resume_actor(actor_t *actor)
{
    restore_executor_globals(&actor->eg);
    // swap back into receive_block
    ph_swap_context(&PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context, &actor->actor_context);
}

void send_local_message(actor_t *to_actor, task_t *task)
{
    message_t *new_message = create_new_message(&task->task.smt.from_actor_ref, task->task.smt.message);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));

    message_t *current_message = to_actor->mailbox;

    if (!current_message) {
        to_actor->mailbox = new_message;
    } else {
        while (current_message->next_message) {
            current_message = current_message->next_message;
        }

        current_message->next_message = new_message;
    }

    if (to_actor->state != ACTIVE_STATE && !to_actor->mailbox->next_message) {
        if (to_actor->state == NEW_STATE) {
            enqueue_task(create_process_message_task(to_actor), to_actor->thread_offset);
        } else {
            enqueue_task(create_resume_actor_task(to_actor), to_actor->thread_offset);
        }
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
}

void send_remote_message(task_t *task)
{
    // @todo debugging purposes only - no implementation yet
    printf("Tried to send a message to a non-existent (or remote) actor\n");
    assert(0);
}

void new_actor(task_t *task)
{
    ph_string_t class_name = task->task.nat.class_name;
    ph_string_t *named_actor_key = task->task.nat.named_actor_key;

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, named_actor_key);
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    assert(named_actor);

    zend_string *class = zend_string_init(PH_STRV(class_name), PH_STRL(class_name), 0);
    zend_class_entry *ce = zend_fetch_class_by_name(class, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
    entry_t *args = task->task.nat.args;
    int argc = task->task.nat.argc;
    zend_function *constructor;
    zval zobj;

    assert(named_actor);
    allowed_to_construct_object = 1;

    if (object_init_ex(&zobj, ce) != SUCCESS) {
        // @todo this will throw an exception in the new thread, rather than at
        // the call site. This doesn't even have an execution context - how
        // should it behave?
        zend_throw_exception_ex(zend_ce_exception, 0, "Failed to create an actor from class '%s'\n", ZSTR_VAL(class));
        zend_string_free(class);
        allowed_to_construct_object = 0;
        return;
    }

    allowed_to_construct_object = 0;

    actor_t *new_actor = (actor_t *)((char *)Z_OBJ(zobj) - Z_OBJ(zobj)->handlers->offset);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    new_actor->name = named_actor_key; // @todo how to best mutex lock this?

    ph_hashtable_insert(&PHACTOR_G(actor_system)->actors, &new_actor->ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    ph_hashtable_insert(&named_actor->actors, &new_actor->ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

    if (constructor) {
        int result;
        zval retval, zargs[argc];
        zend_fcall_info fci;

        for (int i = 0; i < argc; ++i) {
            ph_convert_entry_to_zval(zargs + i, args + i);
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
        // dtor on retval?
    }

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    if (named_actor->status == CONSTRUCTION) {
        named_actor->status = ACTIVE;
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    zend_string_free(class);
}

task_t *create_send_message_task(ph_string_t *from_actor_ref, ph_string_t *to_actor_name, zval *message)
{
    task_t *new_task = malloc(sizeof(task_t));

    new_task->task_type = SEND_MESSAGE_TASK;
    new_task->next_task = NULL;
    ph_string_update(&new_task->task.smt.from_actor_ref, PH_STRV_P(from_actor_ref), PH_STRL_P(from_actor_ref));
    new_task->task.smt.to_actor_name = *to_actor_name;
    new_task->task.smt.message = create_new_entry(message);

    return new_task;
}

message_t *create_new_message(ph_string_t *from_actor_ref, entry_t *message)
{
    message_t *new_message = malloc(sizeof(message_t));

    new_message->from_actor_ref = *from_actor_ref;
    new_message->message = message;
    new_message->next_message = NULL;

    return new_message;
}

task_t *create_process_message_task(actor_t *for_actor)
{
    task_t *new_task = malloc(sizeof(task_t));

    new_task->task.pmt.for_actor = for_actor;
    new_task->task_type = PROCESS_MESSAGE_TASK;
    new_task->next_task = NULL;

    return new_task;
}

task_t *create_new_actor_task(ph_string_t *named_actor_key, zend_string *class_name, zval *args, int argc)
{
    task_t *new_task = malloc(sizeof(task_t));

    new_task->task.nat.named_actor_key = named_actor_key;
    new_task->task.nat.args = NULL;
    new_task->task.nat.argc = argc;
    new_task->task_type = NEW_ACTOR_TASK;
    new_task->next_task = NULL;

    if (argc) {
        new_task->task.nat.args = malloc(sizeof(entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            ph_convert_zval_to_entry(new_task->task.nat.args + i, args + i);
        }
    }

    ph_string_update(&new_task->task.nat.class_name, ZSTR_VAL(class_name), ZSTR_LEN(class_name));

    return new_task;
}

task_t *create_resume_actor_task(actor_t *actor)
{
    task_t *new_task = malloc(sizeof(task_t));

    new_task->task.rat.actor = actor;
    new_task->task_type = RESUME_ACTOR_TASK;
    new_task->next_task = NULL;

    return new_task;
}

void enqueue_task(task_t *new_task, int thread_offset)
{
    thread_t *thread = PHACTOR_G(actor_system)->worker_threads + thread_offset;

    pthread_mutex_lock(&thread->ph_task_mutex);

    task_t *current_task = thread->tasks;

    if (!current_task) {
        thread->tasks = new_task;
    } else {
        while (current_task->next_task) {
            current_task = current_task->next_task;
        }

        current_task->next_task = new_task;
    }

    pthread_mutex_unlock(&thread->ph_task_mutex);
}

task_t *dequeue_task(void)
{
    pthread_mutex_lock(&PH_THREAD_G(ph_task_mutex));

    task_t *task = PH_THREAD_G(tasks);

    if (task) {
        PH_THREAD_G(tasks) = task->next_task;
    }

    pthread_mutex_unlock(&PH_THREAD_G(ph_task_mutex));

    return task;
}

void call_receive_method(zend_object *object, zval *retval_ptr, zval *from_actor, zval *message)
{
    int result;
    zend_fcall_info fci;
    // zend_fcall_info_cache fcc;
    zend_function *receive_function;
    zend_string *receive_function_name;
    zval params[2];

    ZVAL_COPY_VALUE(&params[0], from_actor);
    ZVAL_COPY_VALUE(&params[1], message);

    // receive_function_name = zend_string_init(ZEND_STRL("receive"), 0);
    // receive_function = zend_hash_find_ptr(&object.ce->function_table, receive_function_name); // @todo hashtable consistency problems...

    fci.size = sizeof(fci);
    fci.object = object;
    fci.retval = retval_ptr;
    fci.param_count = 2;
    fci.params = params;
    fci.no_separation = 1;
    ZVAL_STRINGL(&fci.function_name, "receive", sizeof("receive")-1);

    // fcc.initialized = 1;
    // fcc.object = &object;
    // fcc.calling_scope = object.ce;
    // fcc.called_scope = object.ce;
    // fcc.function_handler = receive_function;

    result = zend_call_function(&fci, NULL);

    if (result == FAILURE) { /* error at c-level */
        if (!EG(exception)) {
            zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(object->ce->name), "::", receive_function_name);
        }
    }

    zval_dtor(&fci.function_name);
    // zend_string_free(receive_function_name);
}

// @todo actually generate UUIDs for remote actors
void set_actor_ref(ph_string_t *ref)
{
    ref->len = 33;
    pthread_mutex_lock(&global_actor_id_lock);
    sprintf(ref->val, "%022d%010d", 0, ++global_actor_id);
    pthread_mutex_unlock(&global_actor_id_lock);
}

actor_t *get_actor_from_name(ph_string_t *actor_name)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor_name);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    if (!named_actor) {
        return NULL;
    }

    return ph_hashtable_random_value(&named_actor->actors);
}

actor_t *get_actor_from_ref(ph_string_t *actor_ref)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    actor_t *actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors, actor_ref);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    if (!actor) {
        return NULL;
    }

    // we have to go through the named actors in case the actor has not yet been created
    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor->name);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    assert(named_actor);

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    if (named_actor->status == CONSTRUCTION) {
        pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));
        // This enables for the message to be enqueued again if the actor is
        // still being created
        return (void *) 1; // @todo find a better way?
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    return actor;
}

actor_t *get_actor_from_object(zend_object *actor_obj)
{
    return (actor_t *)((char *)actor_obj - XtOffsetOf(actor_t, obj));
}

actor_t *get_actor_from_zval(zval *actor_zval_obj)
{
    return get_actor_from_object(Z_OBJ_P(actor_zval_obj));
}

void initialise_actor_system()
{
    PHACTOR_G(actor_system)->thread_count = THREAD_COUNT;

    PHACTOR_G(main_thread).id = (ulong) pthread_self();
    PHACTOR_G(main_thread).ls = TSRMLS_CACHE;
    // main_thread.thread = tsrm_thread_id();

    // taken from pthreads - @todo needed still?
    if (Z_TYPE(EG(user_exception_handler)) != IS_UNDEF) {
        if (Z_TYPE_P(&EG(user_exception_handler)) == IS_OBJECT) {
            rebuild_object_properties(Z_OBJ_P(&EG(user_exception_handler)));
        } else if (Z_TYPE_P(&EG(user_exception_handler)) == IS_ARRAY) {
            zval *object = zend_hash_index_find(Z_ARRVAL_P(&EG(user_exception_handler)), 0);
            if (object && Z_TYPE_P(object) == IS_OBJECT) {
                rebuild_object_properties(Z_OBJ_P(object));
            }
        }
    }

    PHACTOR_G(actor_system)->actor_removals = calloc(sizeof(actor_removal_t), PHACTOR_G(actor_system)->thread_count + 1);
    PHACTOR_G(actor_system)->worker_threads = calloc(sizeof(thread_t), PHACTOR_G(actor_system)->thread_count + 1);

    for (int i = 0; i <= PHACTOR_G(actor_system)->thread_count; ++i) {
        thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;
        actor_removal_t *ar = PHACTOR_G(actor_system)->actor_removals + i;

        ar->count = 4;
        ar->actors = malloc(sizeof(actor_t *) * ar->count);

        // set thread->offset = i ?
        thread->offset = i;
        thread->tasks = NULL;
        pthread_mutex_init(&thread->ph_task_mutex, NULL);

        if (i != PHACTOR_G(actor_system)->thread_count) {
            pthread_create((pthread_t *) thread, NULL, (void *) worker_function, thread);
        }
    }

    thread_offset = PHACTOR_G(actor_system)->thread_count;
    thread = PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count;

    while (PHACTOR_G(actor_system)->thread_count != PHACTOR_G(actor_system)->prepared_thread_count);
}

void remove_actor(actor_t *target_actor)
{
    if (target_actor == NULL) { // remote actor
        printf("Freeing remote actor\n"); // when will a remote actor actually be freed?
        return;
    }

    pthread_mutex_lock(&phactor_actors_mutex);
    ph_hashtable_delete(&PHACTOR_G(actor_system)->actors, &target_actor->ref, delete_actor);
    pthread_mutex_unlock(&phactor_actors_mutex);
}

void perform_actor_removals(void)
{
    actor_removal_t *ar = PHACTOR_G(actor_system)->actor_removals + thread_offset;

    pthread_mutex_lock(&actor_removal_mutex);

    for (int i = 0; i < ar->used; ++i) {
        remove_actor(ar->actors[i]);
    }

    ar->used = 0;

    // @todo resize ?

    pthread_mutex_unlock(&actor_removal_mutex);
}

void mark_actor_for_removal(actor_t *actor)
{
    actor_removal_t *ar = PHACTOR_G(actor_system)->actor_removals + actor->thread_offset;

    pthread_mutex_lock(&actor_removal_mutex);

    if (ar->used == ar->count) {
        ar->count <<= 1;
        ar->actors = realloc(ar->actors, sizeof(actor_t *) * ar->count);
    }

    ar->actors[ar->used++] = actor;

    pthread_mutex_unlock(&actor_removal_mutex);
}

void remove_actor_object(zval *actor)
{
    remove_actor(get_actor_from_zval(actor));
}

void php_actor_dtor_object_dummy(zend_object *obj){}
void php_actor_free_object_dummy(zend_object *obj){}

void php_actor_dtor_object(zend_object *obj)
{
    zend_objects_destroy_object(obj);
    zend_object_std_dtor(obj);
}

void php_actor_free_object(zend_object *obj)
{
    actor_t *actor = get_actor_from_object(obj);

    remove_actor(actor);
}

void delete_named_actor(void *named_actor_void)
{
    named_actor_t *named_actor = (named_actor_t *) named_actor_void;

    ph_hashtable_destroy(&named_actor->actors, delete_actor);

    free(named_actor);
}

void delete_actor_from_named_actors(void *actor_void)
{
    // nothing to do
    // the actor has already been deleted, it just needs removing from the ht
}

void delete_actor(void *actor_void)
{
    actor_t *actor = (actor_t *) actor_void;

    // GC_REFCOUNT(&actor->obj) = 0; // @todo needed?

    php_actor_dtor_object(&actor->obj);

    // no need to mutex lock here - when deleting an actor, we should already
    // be holding the phactor_actors_mutex mutex
    named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor->name);

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    ph_hashtable_delete(&named_actor->actors, &actor->ref, delete_actor_from_named_actors);

    --named_actor->perceived_used;

    if (named_actor->perceived_used == 0) {
        delete_named_actor(named_actor); // @todo pass in actor->name instead and remove it from ht
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    free(PH_STRV(actor->ref));
    efree(actor);
}

void php_actor_system_dtor_object(zend_object *obj)
{
    zend_object_std_dtor(obj);

    // ensure threads and other things are freed
}

void php_actor_system_free_object(zend_object *obj)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    ph_hashtable_delete_by_value(&PHACTOR_G(actor_system)->actors, delete_actor, actor_t *, thread_offset, thread_offset);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        free(PHACTOR_G(actor_system)->actor_removals[i].actors);
    }

    free(PHACTOR_G(actor_system)->actor_removals[PHACTOR_G(actor_system)->thread_count].actors);
    free(PHACTOR_G(actor_system)->actor_removals);
    free(PHACTOR_G(actor_system)->actors.values); // @todo should use ph_hashtable_destroy (should be empty)
    free(PHACTOR_G(actor_system)->named_actors.values); // @todo should use ph_hashtable_destroy (should be empty)
}

void receive_block(zval *actor_zval, zval *return_value)
{
    actor_t *actor = get_actor_from_zval(actor_zval);

    if (thread_offset == PHACTOR_G(actor_system)->thread_count) { // if we are in the main thread
        zend_throw_exception(zend_ce_exception, "Trying to receive a message when not in the context of an Actor.", 0);
        return;
    }

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    save_executor_globals(&actor->eg);
    restore_executor_globals(&PHACTOR_G(actor_system)->worker_threads[thread_offset].eg);
    actor->state = IDLE_STATE;

    // possible optimisation: if task queue is empty, just skip the next 7 lines
    if (actor->mailbox) { // @todo check send_local_message to see if this conflicts with it
        enqueue_task(create_resume_actor_task(actor), actor->thread_offset);
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ph_swap_context(&actor->actor_context, &PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    message_t *message = actor->mailbox;
    actor->mailbox = actor->mailbox->next_message;
    actor->state = ACTIVE_STATE;
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ph_convert_entry_to_zval(return_value, message->message);
    free(message);
}

void force_shutdown_actor_system()
{
    pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
    PHACTOR_G(php_shutdown) = 1; // @todo this will not work, since worker threads may still be working
    pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));
}

void scheduler_blocking()
{
    pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
    if (!PHACTOR_G(php_shutdown)) {
        PHACTOR_G(php_shutdown) = !PHACTOR_G(actor_system)->daemonised_actor_system;
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));

    // @todo use own specialised loop here? Only messages should need to be
    // handled in the main thread (for now)
    message_handling_loop();

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        pthread_mutex_destroy(&PHACTOR_G(actor_system)->worker_threads[i].ph_task_mutex);
        pthread_join(PHACTOR_G(actor_system)->worker_threads[i].thread, NULL);
    }

    pthread_mutex_destroy(&PHACTOR_G(actor_system)->worker_threads[PHACTOR_G(actor_system)->thread_count].ph_task_mutex);

    free(PHACTOR_G(actor_system)->worker_threads);
}



ZEND_BEGIN_ARG_INFO(ActorSystem_construct_arginfo, 0)
ZEND_ARG_INFO(0, flag)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(ActorSystem_shutdown_arginfo, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(ActorSystem_block_arginfo, 0)
ZEND_END_ARG_INFO()


ZEND_BEGIN_ARG_INFO_EX(Actor_send_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, actor)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(Actor_remove_arginfo, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(Actor_abstract_receive_arginfo, 0)
    ZEND_ARG_INFO(0, sender)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(Actor_abstract_receiveblock_arginfo, 0)
ZEND_END_ARG_INFO()



/* {{{ proto string ActorSystem::__construct() */
PHP_METHOD(ActorSystem, __construct)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &PHACTOR_G(actor_system)->daemonised_actor_system) != SUCCESS) {
        return;
    }

    if (PHACTOR_G(actor_system)->initialised) {
        zend_throw_exception_ex(NULL, 0, "Actor system already active");
    }

    initialise_actor_system();
}
/* }}} */

/* {{{ proto void ActorSystem::shutdown() */
PHP_METHOD(ActorSystem, shutdown)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    force_shutdown_actor_system();
}
/* }}} */

/* {{{ proto void ActorSystem::block() */
PHP_METHOD(ActorSystem, block)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    scheduler_blocking();
}
/* }}} */


/* {{{ proto string Actor::send(Actor|string $actor, mixed $message) */
PHP_METHOD(Actor, send)
{
    ph_string_t to_actor_name;
    zval *to_actor;
    zval *message;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &to_actor, &message) != SUCCESS) {
        return;
    }

    actor_t *from_actor = get_actor_from_object(Z_OBJ(EX(This)));

    if (Z_TYPE_P(to_actor) == IS_STRING) {
        ph_string_update(&to_actor_name, Z_STRVAL_P(to_actor), Z_STRLEN_P(to_actor));
    } else if (Z_TYPE_P(to_actor) == IS_OBJECT) {
        // enables an actor to send to itself via $this->send($this, ...)
        if (!instanceof_function(Z_OBJCE_P(to_actor), Actor_ce) // @todo enable for ActorRef objects too?
            || from_actor != get_actor_from_object(Z_OBJ_P(to_actor))) {
            zend_throw_exception(NULL, "Sending a message to an object may only be to $this", 0);
            return;
        }

        to_actor_name = *from_actor->name;
    } else {
        zend_throw_exception(NULL, "Unknown recipient value", 0);
        return;
    }

    // @todo For now, we just make the main thread send all of the messages
    // In future, we could create a couple of specialised threads for
    // sending messages only (simplifying task handling for threads)
    enqueue_task(create_send_message_task(&from_actor->ref, &to_actor_name, message), PHACTOR_G(actor_system)->thread_count);
}
/* }}} */

/* {{{ proto string Actor::remove() */
PHP_METHOD(Actor, remove)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    mark_actor_for_removal(get_actor_from_zval(getThis()));
}
/* }}} */

/* {{{ proto string Actor::receiveBlock() */
PHP_METHOD(Actor, receiveBlock)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    receive_block(getThis(), return_value);
}
/* }}} */



/* {{{ */
zend_function_entry ActorSystem_methods[] = {
    PHP_ME(ActorSystem, __construct, ActorSystem_construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ActorSystem, shutdown, ActorSystem_shutdown_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(ActorSystem, block, ActorSystem_block_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
}; /* }}} */

/* {{{ */
zend_function_entry Actor_methods[] = {
    PHP_ME(Actor, send, Actor_send_arginfo, ZEND_ACC_PROTECTED)
    PHP_ME(Actor, remove, Actor_remove_arginfo, ZEND_ACC_PUBLIC)
    PHP_ABSTRACT_ME(Actor, receive, Actor_abstract_receive_arginfo)
    PHP_ME(Actor, receiveBlock, Actor_abstract_receiveblock_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
}; /* }}} */



zend_object* phactor_actor_ctor(zend_class_entry *entry)
{
    actor_t *new_actor = ecalloc(1, sizeof(actor_t) + zend_object_properties_size(entry));

    new_actor->thread_offset = thread_offset;

    zend_object_std_init(&new_actor->obj, entry);
    object_properties_init(&new_actor->obj, entry);

    new_actor->obj.handlers = &phactor_actor_handlers;

    if (!allowed_to_construct_object) {
        zend_throw_exception(zend_ce_error, "Actors cannot be created via class instantiation - please use register() instead", 0);
        return &new_actor->obj;
    }

    /*
    Prevents an actor from being destroyed automatically.
    */
    ++GC_REFCOUNT(&new_actor->obj); // @todo necessary still?
    new_actor->state = NEW_STATE;

    PH_STRL(new_actor->ref) = ACTOR_REF_LEN;
    PH_STRV(new_actor->ref) = malloc(sizeof(char) * ACTOR_REF_LEN);
    set_actor_ref(&new_actor->ref);

    ph_init_context(&new_actor->actor_context, process_message);

    return &new_actor->obj;
}

static zend_object* phactor_actor_system_ctor(zend_class_entry *entry)
{
    if (!PHACTOR_G(actor_system)) {
        PHACTOR_G(actor_system) = ecalloc(1, sizeof(actor_system_t) + zend_object_properties_size(entry));

        // @todo create the UUID on actor creation - this is needed for remote actor systems only

        ph_hashtable_init(&PHACTOR_G(actor_system)->actors, 8);
        ph_hashtable_init(&PHACTOR_G(actor_system)->named_actors, 8);

        zend_object_std_init(&PHACTOR_G(actor_system)->obj, entry);
        object_properties_init(&PHACTOR_G(actor_system)->obj, entry);

        PHACTOR_G(actor_system)->obj.handlers = &phactor_actor_system_handlers;
    }

    return &PHACTOR_G(actor_system)->obj;
}

named_actor_t *new_named_actor(void)
{
    named_actor_t *named_actor = malloc(sizeof(named_actor_t));

    named_actor->status = CONSTRUCTION;
    named_actor->perceived_used = 0;
    ph_hashtable_init(&named_actor->actors, 1);

    return named_actor;
}

zend_long register_new_actor(zend_string *name, zend_string *class, zval *args, int argc)
{
    zend_function *constructor;
    zval zobj;
    ph_string_t key, *key2;
    named_actor_t *named_actor;

    if (!PHACTOR_G(actor_system)) {
        zend_throw_exception(zend_ce_error, "The ActorSystem class must first be instantiated", 0);
        return 0;
    }

    if (!zend_fetch_class_by_name(class, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION)) {
        return 0;
    }

    ph_string_update(&key, ZSTR_VAL(name), ZSTR_LEN(name));

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, &key);
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    if (!named_actor) {
        key2 = malloc(sizeof(ph_string_t));

        PH_STRL_P(key2) = PH_STRL(key);
        PH_STRV_P(key2) = PH_STRV(key);

        named_actor = new_named_actor();

        pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
        ph_hashtable_insert(&PHACTOR_G(actor_system)->named_actors, key2, named_actor);
        pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));
    } else {
        pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
        key2 = ph_hashtable_key_fetch(&PHACTOR_G(actor_system)->named_actors, &key);
        pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));
    }

    task_t *task = create_new_actor_task(key2, class, args, argc);
    int thread_offset = rand() % PHACTOR_G(actor_system)->thread_count; // @todo modulo bias; don't bother with main thread?

    enqueue_task(task, thread_offset);

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    int new_count = ++named_actor->perceived_used;
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    return new_count;
}

ZEND_BEGIN_ARG_INFO_EX(register_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_INFO(0, class)
ZEND_END_ARG_INFO()

// int register(string $name, string $class, mixed ...$args);
PHP_FUNCTION(register)
{
    zend_string *name, *class;
    zval *args;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(2, -1)
        Z_PARAM_STR(name)
        Z_PARAM_STR(class)
        Z_PARAM_VARIADIC('+', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    RETVAL_LONG(register_new_actor(name, class, args, argc));
}

const zend_function_entry phactor_functions[] = {
    PHP_FE(register, register_arginfo)
    PHP_FE_END
};

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(phactor)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    /* ActorSystem Class */
    INIT_CLASS_ENTRY(ce, "ActorSystem", ActorSystem_methods);
    ActorSystem_ce = zend_register_internal_class(&ce);
    ActorSystem_ce->create_object = phactor_actor_system_ctor;

    memcpy(&phactor_actor_system_handlers, zh, sizeof(zend_object_handlers));

    phactor_actor_system_handlers.offset = XtOffsetOf(actor_system_t, obj);
    phactor_actor_system_handlers.dtor_obj = php_actor_system_dtor_object;
    phactor_actor_system_handlers.free_obj = php_actor_system_free_object;

    /* Actor Class */
    INIT_CLASS_ENTRY(ce, "Actor", Actor_methods);
    Actor_ce = zend_register_internal_class(&ce);
    Actor_ce->ce_flags |= ZEND_ACC_ABSTRACT;
    Actor_ce->create_object = phactor_actor_ctor;

    memcpy(&phactor_actor_handlers, zh, sizeof(zend_object_handlers));

    phactor_actor_handlers.offset = XtOffsetOf(actor_t, obj);
    phactor_actor_handlers.dtor_obj = php_actor_dtor_object_dummy;
    phactor_actor_handlers.free_obj = php_actor_free_object_dummy;

    PHACTOR_G(phactor_instance) = TSRMLS_CACHE;

    pthread_mutex_init(&phactor_mutex, NULL);
    pthread_mutex_init(&phactor_actors_mutex, NULL);
    pthread_mutex_init(&phactor_named_actors_mutex, NULL);
    pthread_mutex_init(&actor_removal_mutex, NULL); // @todo optimise by specialising per thread
    pthread_mutex_init(&global_actor_id_lock, NULL);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(phactor)
{
    pthread_mutex_destroy(&phactor_mutex);
    pthread_mutex_destroy(&phactor_actors_mutex);
    pthread_mutex_destroy(&phactor_named_actors_mutex);
    pthread_mutex_destroy(&actor_removal_mutex);
    pthread_mutex_destroy(&global_actor_id_lock);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(phactor)
{
    TSRMLS_CACHE_UPDATE();

    // @todo remove, not needed?
    if (PHACTOR_G(phactor_instance) != TSRMLS_CACHE) {
        if (memcmp(sapi_module.name, ZEND_STRL("cli")) == SUCCESS) {
            sapi_module.deactivate = NULL;
        }
    }

    zend_hash_init(&PHACTOR_ZG(interned_strings), 8, NULL, ZVAL_PTR_DTOR, 0);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(phactor)
{
    zend_hash_destroy(&PHACTOR_ZG(interned_strings));

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(phactor)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "phactor support", "enabled");
    php_info_print_table_end();
}
/* }}} */

PHP_GINIT_FUNCTION(phactor)
{
    // pthread_mutexattr_t at;
    // pthread_mutexattr_init(&at);

    // #if defined(PTHREAD_MUTEX_RECURSIVE) || defined(__FreeBSD__)
    // pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    // #else
    // pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE_NP);
    // #endif

    // pthread_mutex_init(&phactor_mutex, &at);

    pthread_mutex_init(&phactor_mutex, NULL);
    // pthread_mutex_init(&ph_task_mutex, NULL);
    pthread_mutex_init(&phactor_actors_mutex, NULL);
}

PHP_GSHUTDOWN_FUNCTION(phactor)
{
    pthread_mutex_destroy(&phactor_mutex);
    // pthread_mutex_destroy(&ph_task_mutex);
    pthread_mutex_destroy(&phactor_actors_mutex);
}

/* {{{ phactor_module_entry */
zend_module_entry phactor_module_entry = {
    STANDARD_MODULE_HEADER,
    "phactor",
    phactor_functions,
    PHP_MINIT(phactor),
    PHP_MSHUTDOWN(phactor),
    PHP_RINIT(phactor),
    PHP_RSHUTDOWN(phactor),
    PHP_MINFO(phactor),
    PHP_PHACTOR_VERSION,
    PHP_MODULE_GLOBALS(phactor),
    NULL, //PHP_GINIT(phactor),
    NULL, //PHP_GSHUTDOWN(phactor),
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

#ifdef COMPILE_DL_PHACTOR
TSRMLS_CACHE_DEFINE()
ZEND_GET_MODULE(phactor)
#endif
