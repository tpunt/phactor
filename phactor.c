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
void process_message(task_t *task);
void enqueue_task(task_t *task);
task_t *dequeue_task(void);
void call_receive_method(zend_object *object, zval *retval_ptr, zval *from_actor, zval *message);
actor_t *get_actor_from_ref(ph_string_t *actor_ref);
actor_t *get_actor_from_object(zend_object *actor_obj);
actor_t *get_actor_from_zval(zval *actor_zval_obj);
task_t *create_send_message_task(char *from_actor_ref, char *to_actor_ref, zval *message);
task_t *create_process_message_task(actor_t *for_actor);
zend_object* phactor_actor_ctor(zend_class_entry *entry);
void delete_actor(void *actor);
void add_new_actor(actor_t *new_actor);
message_t *create_new_message(ph_string_t *from_actor_ref, entry_t *message);
void send_message(task_t *task);
void send_local_message(actor_t *to_actor, task_t *task);
void send_remote_message(task_t *task);
void initialise_actor_system(void);
void perform_actor_removals(void);
void mark_actor_for_removal(actor_t *actor);

#define ACTOR_REF_LEN 33

static __thread int thread_offset;

thread_t main_thread;
pthread_mutex_t phactor_mutex;
pthread_mutex_t phactor_task_mutex;
pthread_mutex_t phactor_actors_mutex;
pthread_mutex_t actor_removal_mutex;
pthread_mutex_t global_actor_id_lock;
actor_system_t *actor_system;
int php_shutdown = 0;
int global_actor_id = 0;
dtor_func_t (default_resource_dtor);
zend_object_handlers phactor_actor_handlers;
zend_object_handlers phactor_actor_system_handlers;
void ***phactor_instance = NULL;

zend_class_entry *ActorSystem_ce;
zend_class_entry *Actor_ce;

void *worker_function(thread_t *phactor_thread)
{
    thread_offset = phactor_thread->offset;
    phactor_thread->id = (ulong) pthread_self();
    phactor_thread->ls = ts_resource(0);

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
                process_message(current_task);
                break;
        }

        free(current_task);
    }

    // zend_hash_apply(&EG(regular_list), pthreads_resources_cleanup); // ignore resource for now

    PG(report_memleaks) = 0;

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    ph_hashtable_delete_by_value(&PHACTOR_G(actor_system)->actors, delete_actor, actor_t *, thread_offset, thread_offset);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    // block here until all other threads have finished executing? This will prevent
    // prematurely freeing actors that are still be executed by other threads

    // free all actors associated with this interpreter instance

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

void process_message(task_t *task)
{
    actor_t *for_actor = task->task.pmt.for_actor;
    zval return_value, from_actor_zval, message_zval;

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    message_t *message = for_actor->mailbox;
    for_actor->mailbox = for_actor->mailbox->next_message;
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ZVAL_STR(&from_actor_zval, zend_string_init(PH_STRV(message->from_actor_ref), PH_STRL(message->from_actor_ref), 0));
    ph_entry_convert(&message_zval, message->message);

    call_receive_method(&for_actor->obj, &return_value, &from_actor_zval, &message_zval);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    for_actor->in_execution = 0;
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    free(PH_STRV(message->from_actor_ref));
    zval_ptr_dtor(&message_zval);
    delete_entry(message->message);
    free(message);

    zval_ptr_dtor(&from_actor_zval);
    zval_ptr_dtor(&return_value); // @todo how to handle the return value?
}

void send_message(task_t *task)
{
    actor_t *to_actor = get_actor_from_ref(&task->task.smt.to_actor_ref);

    if (to_actor) {
        send_local_message(to_actor, task);
    } else {
        send_remote_message(task);
    }

    free(PH_STRV(task->task.smt.to_actor_ref));
}

/*
Add the message (containing the from_actor and message) to the to_actor's mailbox.
Enqueue the to_actor as a new task to have its mailbox processed.
*/
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

    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    enqueue_task(create_process_message_task(to_actor));
}

void send_remote_message(task_t *task)
{
    // @todo debugging purposes only - no implementation yet
    printf("Tried to send a message to a non-existent (or remote) actor\n");
    assert(0);
}

task_t *create_send_message_task(char *from_actor_ref, char *to_actor_ref, zval *message)
{
    task_t *new_task = malloc(sizeof(task_t));

    new_task->task_type = SEND_MESSAGE_TASK;
    new_task->next_task = NULL;
    PH_STRV(new_task->task.smt.from_actor_ref) = malloc(sizeof(char) * ACTOR_REF_LEN);
    PH_STRL(new_task->task.smt.from_actor_ref) = ACTOR_REF_LEN;
    memcpy(PH_STRV(new_task->task.smt.from_actor_ref), from_actor_ref, ACTOR_REF_LEN);
    PH_STRV(new_task->task.smt.to_actor_ref) = malloc(sizeof(char) * ACTOR_REF_LEN);
    PH_STRL(new_task->task.smt.to_actor_ref) = ACTOR_REF_LEN;
    memcpy(PH_STRV(new_task->task.smt.to_actor_ref), to_actor_ref, ACTOR_REF_LEN);
    new_task->task.smt.message = create_new_entry(message, 0);

    return new_task;
}

message_t *create_new_message(ph_string_t *from_actor_ref, entry_t *message)
{
    message_t *new_message = malloc(sizeof(message_t));

    // new_message->from_actor_ref = *from_actor_ref
    PH_STRL(new_message->from_actor_ref) = PH_STRL_P(from_actor_ref);
    PH_STRV(new_message->from_actor_ref) = PH_STRV_P(from_actor_ref);
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

void enqueue_task(task_t *new_task)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_task_mutex));

    task_t *current_task = PHACTOR_G(actor_system)->tasks;

    if (!current_task) {
        PHACTOR_G(actor_system)->tasks = new_task;
    } else {
        while (current_task->next_task) {
            current_task = current_task->next_task;
        }

        current_task->next_task = new_task;
    }

    pthread_mutex_unlock(&PHACTOR_G(phactor_task_mutex));
}

task_t *dequeue_task(void)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_task_mutex));

    task_t *task = PHACTOR_G(actor_system)->tasks;
    task_t *prev_task = task;

    if (!task) {
        pthread_mutex_unlock(&PHACTOR_G(phactor_task_mutex));
        return NULL;
    }

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    if (task->task_type & SEND_MESSAGE_TASK || !task->task.pmt.for_actor->in_execution) {
        if (task->task_type & PROCESS_MESSAGE_TASK) {
            task->task.pmt.for_actor->in_execution = 1;
        }
        pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

        PHACTOR_G(actor_system)->tasks = task->next_task;

        pthread_mutex_unlock(&PHACTOR_G(phactor_task_mutex));
        return task;
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    while (1) {
        if (!task) {
            break;
        }

        if (task->task_type & SEND_MESSAGE_TASK) {
            break;
        }

        pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
        if (!task->task.pmt.for_actor->in_execution) {
            task->task.pmt.for_actor->in_execution = 1;
            pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
            break;
        }
        pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

        prev_task = task;
        task = task->next_task;
    }

    if (task) {
        prev_task->next_task = task->next_task;
    }

    pthread_mutex_unlock(&PHACTOR_G(phactor_task_mutex));

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

actor_t *get_actor_from_ref(ph_string_t *actor_ref)
{
    return ph_hashtable_search(&PHACTOR_G(actor_system)->actors, actor_ref);
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
    PHACTOR_G(actor_system)->tasks = NULL;
    PHACTOR_G(actor_system)->thread_count = 1;//sysconf(_SC_NPROCESSORS_ONLN);

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

    thread_offset = PHACTOR_G(actor_system)->thread_count;
    PHACTOR_G(main_thread).offset = PHACTOR_G(actor_system)->thread_count;

    PHACTOR_G(actor_system)->actor_removals = calloc(sizeof(actor_removal_t), PHACTOR_G(actor_system)->thread_count + 1);
    PHACTOR_G(actor_system)->worker_threads = malloc(sizeof(thread_t) * PHACTOR_G(actor_system)->thread_count);

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        thread_t *thread = PHACTOR_G(actor_system)->worker_threads + i;
        actor_removal_t *ar = PHACTOR_G(actor_system)->actor_removals + i;

        ar->count = 4;
        ar->actors = malloc(sizeof(actor_t *) * ar->count);

        thread->offset = i;
        pthread_create((pthread_t *) thread, NULL, (void *) worker_function, thread);
    }

    actor_removal_t *ar = PHACTOR_G(actor_system)->actor_removals + PHACTOR_G(main_thread).offset;

    ar->count = 4;
    ar->actors = malloc(sizeof(actor_t *) * ar->count);

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

    ph_hashtable_destroy(&actor->store.props, delete_entry);

    remove_actor(actor);
}

void delete_actor(void *actor_void)
{
    actor_t *actor = (actor_t *) actor_void;

    // GC_REFCOUNT(&actor->obj) = 0; // @todo needed?

    php_actor_dtor_object(&actor->obj);
    ph_hashtable_destroy(&actor->store.props, delete_entry);

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

    free(PHACTOR_G(actor_system)->actor_removals[PHACTOR_G(main_thread).offset].actors);
    free(PHACTOR_G(actor_system)->actor_removals);
    free(PHACTOR_G(actor_system)->actors.values);
    efree(PHACTOR_G(actor_system));
}

// @todo currently impossible with current ZE
void receive_block(zval *actor_zval, zval *return_value)
{
    // actor_t *actor = get_actor_from_zval(actor_zval);

    // zend_execute_data *execute_data = EG(current_execute_data);

    // actor->state = zend_freeze_call_stack(EG(current_execute_data));
    // actor->return_value = actor_zval; // tmp

    // EG(current_execute_data) = NULL;

    // return PHACTOR_ZG(current_message_value); // not the solution...
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
    PHACTOR_G(php_shutdown) = !PHACTOR_G(actor_system)->daemonised_actor_system;
    pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));

    while (1) {
        perform_actor_removals();
        pthread_mutex_lock(&PHACTOR_G(phactor_mutex));
        if (PHACTOR_G(php_shutdown)) {
            pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));
            break;
        }
        pthread_mutex_unlock(&PHACTOR_G(phactor_mutex));
    }

    for (int i = 0; i < PHACTOR_G(actor_system)->thread_count; ++i) {
        pthread_join(PHACTOR_G(actor_system)->worker_threads[i].thread, NULL);
    }

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
    char *to_actor_ref;
    zval *to_actor;
    zval *message;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &to_actor, &message) != SUCCESS) {
        return;
    }

    // printf("-> %p\nsc %p\n", Z_OBJ(EX(This))->handle, )

    if (Z_TYPE_P(to_actor) == IS_STRING) {
        if (Z_STRLEN_P(to_actor) != ACTOR_REF_LEN) {
            // zend_throw_exception_ex(NULL, )
        }

        to_actor_ref = Z_STRVAL_P(to_actor);
    } else if (Z_TYPE_P(to_actor) == IS_OBJECT) {
        if (!instanceof_function(Z_OBJCE_P(to_actor), Actor_ce)) {
            // zend_throw_exception_ex(NULL, )
        }

        to_actor_ref = PH_STRV(get_actor_from_object(Z_OBJ_P(to_actor))->ref);
    } else {
        // zend_throw_exception_ex(NULL, )
    }

    enqueue_task(create_send_message_task(PH_STRV(get_actor_from_object(Z_OBJ(EX(This)))->ref), to_actor_ref, message));
}
/* }}} */

/* {{{ proto string Actor::remove() */
PHP_METHOD(Actor, remove)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    mark_actor_for_removal(get_actor_from_zval(getThis()));
    // remove_actor_object(getThis());
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
    PHP_ME(ActorSystem, shutdown, ActorSystem_shutdown_arginfo, ZEND_ACC_PUBLIC)
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

    if (!PHACTOR_G(actor_system)) {
        zend_throw_exception(zend_ce_error, "The ActorSystem class must first be instantiated", 0);
        return &new_actor->obj;
    }

    /*
    The following prevents an actor that has been created and not used from being
    instantly destroyed by the VM (given that it will be a TMP value).
    */
    ++GC_REFCOUNT(&new_actor->obj);

    PH_STRL(new_actor->ref) = ACTOR_REF_LEN;
    PH_STRV(new_actor->ref) = malloc(sizeof(char) * ACTOR_REF_LEN);
    set_actor_ref(&new_actor->ref);

    new_actor->store.ce = entry;
    ph_hashtable_init(&new_actor->store.props, 8);
    new_actor->store.props.flags |= FREE_KEYS;

    zend_string *key;
    zend_property_info *value;

    ZEND_HASH_FOREACH_STR_KEY_PTR(&entry->properties_info, key, value) {
        ph_store_add(&new_actor->store, key, OBJ_PROP(&new_actor->obj, value->offset), value->flags);
    } ZEND_HASH_FOREACH_END();

    add_new_actor(new_actor);

    return &new_actor->obj;
}

void add_new_actor(actor_t *new_actor)
{
    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    ph_hashtable_insert(&PHACTOR_G(actor_system)->actors, &new_actor->ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));
}

static zend_object* phactor_actor_system_ctor(zend_class_entry *entry)
{
    if (!PHACTOR_G(actor_system)) {
        PHACTOR_G(actor_system) = ecalloc(1, sizeof(actor_system_t) + zend_object_properties_size(entry));

        // @todo create the UUID on actor creation - this is needed for remote actor systems only

        ph_hashtable_init(&PHACTOR_G(actor_system)->actors, 8);

        zend_object_std_init(&PHACTOR_G(actor_system)->obj, entry);
        object_properties_init(&PHACTOR_G(actor_system)->obj, entry);

        PHACTOR_G(actor_system)->obj.handlers = &phactor_actor_system_handlers;
    }

    return &PHACTOR_G(actor_system)->obj;
}

HashTable *phactor_actor_get_debug_info(zval *actor_zval, int *is_temp)
{
    HashTable *ht = emalloc(sizeof(HashTable));
    actor_t *actor = get_actor_from_object(Z_OBJ_P(actor_zval));

    zend_hash_init(ht, 8, NULL, ZVAL_PTR_DTOR, 0);
    *is_temp = 1;
    ph_store_to_hashtable(ht, &actor->store);

    return ht;
}

HashTable *phactor_actor_get_properties(zval *actor_zval)
{
    zend_object *actor_obj = Z_OBJ_P(actor_zval);
    actor_t *actor = get_actor_from_object(actor_obj);

    rebuild_object_properties(actor_obj);

    ph_store_to_hashtable(actor_obj->properties, &actor->store);

    return zend_std_get_properties(actor_zval);
}

void php_actor_write_property(zval *actor_zval, zval *member, zval *value, void **cache)
{
    actor_t *actor = get_actor_from_zval(actor_zval);

    // @todo take into account __set

    ph_store_add(&actor->store, Z_STR_P(member), value, ZEND_ACC_PUBLIC);

    // _zend_hash_str_add(actor_obj->properties, b->key.val, b->key.len, &value ZEND_FILE_LINE_CC);
    // rebuild_object_properties(&get_actor_from_object(Z_OBJ_P(object))->obj);
}

zval *php_actor_read_property(zval *actor_zval, zval *member, int type, void **cache, zval *rv)
{
    actor_t *actor = get_actor_from_zval(actor_zval);
    zval *this = &EG(current_execute_data)->This;

    if (Z_TYPE_P(this) != IS_OBJECT) {
        this = NULL;
    }

    ph_store_read(&actor->store, Z_STR_P(member), rv, this); // getThis() type?

    return rv;
}

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
    phactor_actor_handlers.write_property = php_actor_write_property;
    phactor_actor_handlers.read_property = php_actor_read_property;
    phactor_actor_handlers.get_property_ptr_ptr = NULL;
    phactor_actor_handlers.dtor_obj = php_actor_dtor_object_dummy;
    phactor_actor_handlers.free_obj = php_actor_free_object_dummy;
    phactor_actor_handlers.get_debug_info = phactor_actor_get_debug_info;
    phactor_actor_handlers.get_properties = phactor_actor_get_properties;

    PHACTOR_G(phactor_instance) = TSRMLS_CACHE;

    pthread_mutex_init(&phactor_mutex, NULL);
    pthread_mutex_init(&phactor_task_mutex, NULL);
    pthread_mutex_init(&phactor_actors_mutex, NULL);
    pthread_mutex_init(&actor_removal_mutex, NULL); // @todo optimise by specialising per thread
    pthread_mutex_init(&global_actor_id_lock, NULL);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(phactor)
{
    pthread_mutex_destroy(&phactor_mutex);
    pthread_mutex_destroy(&phactor_task_mutex);
    pthread_mutex_destroy(&phactor_actors_mutex);
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
    pthread_mutex_init(&phactor_task_mutex, NULL);
    pthread_mutex_init(&phactor_actors_mutex, NULL);
}

PHP_GSHUTDOWN_FUNCTION(phactor)
{
    pthread_mutex_destroy(&phactor_mutex);
    pthread_mutex_destroy(&phactor_task_mutex);
    pthread_mutex_destroy(&phactor_actors_mutex);
}

/* {{{ phactor_module_entry */
zend_module_entry phactor_module_entry = {
    STANDARD_MODULE_HEADER,
    "phactor",
    NULL,
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
