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

#include "php_phactor.h"
#include "src/ph_task.h"
#include "src/ph_debug.h"
#include "src/ph_message.h"
#include "src/classes/actor_system.h"

#define ACTOR_REF_LEN 33

extern ph_actor_system_t *actor_system;
extern __thread ph_task_t *currently_processing_task;
extern __thread int thread_offset;

pthread_mutex_t global_actor_id_lock;
int global_actor_id;

zend_object_handlers phactor_actor_handlers;
zend_class_entry *Actor_ce;

static ph_actor_t *ph_actor_retrieve_from_object(zend_object *actor_obj)
{
    return (ph_actor_t *)((char *)actor_obj - XtOffsetOf(ph_actor_t, obj));
}

static ph_actor_t *ph_actor_retrieve_from_zval(zval *actor_zval_obj)
{
    return ph_actor_retrieve_from_object(Z_OBJ_P(actor_zval_obj));
}

ph_actor_t *ph_actor_retrieve_from_name(ph_string_t *actor_name)
{
    ph_named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor_name);

    if (!named_actor) {
        return NULL;
    }

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    if (named_actor->state == PH_NAMED_ACTOR_CONSTRUCTING) {
        pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));
        // This enables for the message to be enqueued again if the actor is
        // still being created
        return (void *) 1; // @todo find a better way?
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    return ph_hashtable_random_value(&named_actor->actors);
}

ph_actor_t *ph_actor_retrieve_from_ref(ph_string_t *actor_ref)
{
    ph_actor_t *actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors, actor_ref);

    if (!actor) {
        return NULL;
    }

    // we have to go through the named actors in case the actor has not yet been created
    ph_named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor->name);

    assert(named_actor);

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    if (named_actor->state == PH_NAMED_ACTOR_CONSTRUCTING) {
        pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));
        // This enables for the message to be enqueued again if the actor is
        // still being created
        return (void *) 1; // @todo find a better way?
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    return actor;
}

// @todo actually generate UUIDs for remote actors
static void ph_actor_set_ref(ph_string_t *ref)
{
    ref->len = ACTOR_REF_LEN;
    pthread_mutex_lock(&global_actor_id_lock);
    sprintf(ref->val, "%022d%010d", 0, ++global_actor_id);
    pthread_mutex_unlock(&global_actor_id_lock);
}

void ph_actor_remove(void *target_actor_void)
{
    if (target_actor_void == NULL) { // remote actor
        printf("Freeing remote actor\n"); // when will a remote actor actually be freed?
        return;
    }

    ph_actor_t *target_actor = target_actor_void;

    ph_hashtable_delete(&PHACTOR_G(actor_system)->actors, &target_actor->ref, ph_actor_free);
}

static void ph_actor_mark_for_removal(ph_actor_t *actor)
{
    ph_vector_t *actor_removals = PHACTOR_G(actor_system)->actor_removals + actor->thread_offset;

    pthread_mutex_lock(&actor_removals->lock);
    ph_vector_push(actor_removals, actor);
    pthread_mutex_unlock(&actor_removals->lock);
}

static void ph_actor_dtor_object_dummy(zend_object *obj){}
static void ph_actor_free_object_dummy(zend_object *obj){}

static void ph_actor_dtor_object(zend_object *obj)
{
    zend_objects_destroy_object(obj);
    zend_object_std_dtor(obj);
}

static void ph_named_actor_free(void *named_actor_void)
{
    ph_named_actor_t *named_actor = (ph_named_actor_t *) named_actor_void;

    ph_hashtable_destroy(&named_actor->actors, ph_actor_free);

    free(named_actor);
}

static void ph_actor_remove_from_named_actors(void *actor_void)
{
    // nothing to do
    // the actor has already been deleted, it just needs removing from the ht
}

void ph_actor_free(void *actor_void)
{
    ph_actor_t *actor = (ph_actor_t *) actor_void;

    // GC_REFCOUNT(&actor->obj) = 0; // @todo needed?

    ph_actor_dtor_object(&actor->obj);

    ph_named_actor_t *named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, actor->name);

    ph_hashtable_delete(&named_actor->actors, &actor->ref, ph_actor_remove_from_named_actors);

    pthread_mutex_lock(&PHACTOR_G(phactor_named_actors_mutex));
    --named_actor->perceived_used;

    if (named_actor->perceived_used == 0) {
        ph_named_actor_free(named_actor); // @todo pass in actor->name instead and remove it from ht
    }
    pthread_mutex_unlock(&PHACTOR_G(phactor_named_actors_mutex));

    ph_str_value_free(&actor->ref);
    efree(actor);
}

static void receive_block(zval *actor_zval, zval *return_value)
{
    ph_actor_t *actor = ph_actor_retrieve_from_zval(actor_zval);

    if (thread_offset == PHACTOR_G(actor_system)->thread_count) { // if we are in the main thread
        zend_throw_exception(zend_ce_exception, "Trying to receive a message when not in the context of an Actor.", 0);
        return;
    }

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    ph_executor_globals_save(&actor->eg);
    ph_executor_globals_restore(&PHACTOR_G(actor_system)->worker_threads[thread_offset].eg);
    actor->state = PH_ACTOR_IDLE;

    // @todo possible optimisation: if task queue is empty, just skip the next 7 lines
    pthread_mutex_lock(&actor->mailbox.lock);
    if (ph_queue_size(&actor->mailbox)) { // @todo check send_local_message to see if this conflicts with it
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + actor->thread_offset;
        ph_task_t *task = ph_task_create_resume_actor(actor);

        pthread_mutex_lock(&thread->tasks.lock);
        ph_queue_push(&thread->tasks, task);
        pthread_mutex_unlock(&thread->tasks.lock);
    }
    pthread_mutex_unlock(&actor->mailbox.lock);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ph_context_swap(&actor->actor_context, &PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context);

    pthread_mutex_lock(&actor->mailbox.lock);
    ph_message_t *message = ph_queue_pop(&actor->mailbox);
    pthread_mutex_unlock(&actor->mailbox.lock);

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    actor->state = PH_ACTOR_ACTIVE;
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ph_entry_convert_to_zval(return_value, message->message);
    ph_msg_free(message);
}

static void call_receive_method(zend_object *object, zval *retval_ptr, zval *from_actor, zval *message)
{
    int result;
    zend_fcall_info fci;
    zend_function *receive_function;
    zval params[2];

    ZVAL_COPY_VALUE(&params[0], from_actor);
    ZVAL_COPY_VALUE(&params[1], message);

    fci.size = sizeof(fci);
    fci.object = object;
    fci.retval = retval_ptr;
    fci.param_count = 2;
    fci.params = params;
    fci.no_separation = 1;
    ZVAL_STRINGL(&fci.function_name, "receive", sizeof("receive")-1);

    result = zend_call_function(&fci, NULL);

    if (result == FAILURE && !EG(exception)) {
        zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(object->ce->name), "::receive");
    }

    zval_dtor(&fci.function_name);
}

void process_message(/*ph_task_t *task*/)
{
    ph_task_t *task = currently_processing_task;
    ph_actor_t *for_actor = task->u.pmt.for_actor;
    zval return_value, from_actor_zval, message_zval;

    pthread_mutex_lock(&PHACTOR_G(phactor_actors_mutex));
    pthread_mutex_lock(&for_actor->mailbox.lock);
    ph_message_t *message = ph_queue_pop(&for_actor->mailbox);
    for_actor->state = PH_ACTOR_ACTIVE;
    pthread_mutex_unlock(&for_actor->mailbox.lock);
    pthread_mutex_unlock(&PHACTOR_G(phactor_actors_mutex));

    ZVAL_STR(&from_actor_zval, zend_string_init(PH_STRV(message->from_actor_ref), PH_STRL(message->from_actor_ref), 0));
    ph_entry_convert_to_zval(&message_zval, message->message);

    call_receive_method(&for_actor->obj, &return_value, &from_actor_zval, &message_zval);

    zval_ptr_dtor(&message_zval);
    ph_msg_free(message);

    zval_ptr_dtor(&from_actor_zval);
    zval_ptr_dtor(&return_value);

    ph_context_set(&PHACTOR_G(actor_system)->worker_threads[thread_offset].thread_context);
}

zend_object* phactor_actor_ctor(zend_class_entry *entry)
{
    ph_actor_t *new_actor = ecalloc(1, sizeof(ph_actor_t) + zend_object_properties_size(entry));

    new_actor->thread_offset = thread_offset;

    zend_object_std_init(&new_actor->obj, entry);
    object_properties_init(&new_actor->obj, entry);

    new_actor->obj.handlers = &phactor_actor_handlers;

    if (!PHACTOR_ZG(allowed_to_construct_object)) {
        zend_throw_exception(zend_ce_error, "Actors cannot be created via class instantiation - please use spawn() instead", 0);
        return &new_actor->obj;
    }

    /*
    Prevents an actor from being destroyed automatically.
    */
    ++GC_REFCOUNT(&new_actor->obj); // @todo necessary still?
    new_actor->state = PH_ACTOR_NEW;

    PH_STRV(new_actor->ref) = malloc(sizeof(char) * ACTOR_REF_LEN);
    ph_actor_set_ref(&new_actor->ref);
    ph_queue_init(&new_actor->mailbox, ph_msg_free);
    ph_context_init(&new_actor->actor_context, process_message);

    return &new_actor->obj;
}

ZEND_BEGIN_ARG_INFO_EX(Actor_send_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, actor)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

PHP_METHOD(Actor, send)
{
    ph_string_t to_actor_name;
    zval *to_actor;
    zval *message;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &to_actor, &message) != SUCCESS) {
        return;
    }

    ph_actor_t *from_actor = ph_actor_retrieve_from_object(Z_OBJ(EX(This)));

    if (Z_TYPE_P(to_actor) == IS_STRING) {
        ph_str_set(&to_actor_name, Z_STRVAL_P(to_actor), Z_STRLEN_P(to_actor));
    } else if (Z_TYPE_P(to_actor) == IS_OBJECT) {
        // enables an actor to send to itself via $this->send($this, ...)
        if (!instanceof_function(Z_OBJCE_P(to_actor), Actor_ce) // @todo enable for ActorRef objects too?
            || from_actor != ph_actor_retrieve_from_object(Z_OBJ_P(to_actor))) {
            zend_throw_exception(NULL, "Sending a message to an object may only be to $this", 0);
            return;
        }

        to_actor_name = *from_actor->name;
    } else {
        zend_throw_exception(NULL, "Unknown recipient value", 0);
        return;
    }

    ph_task_t *task = ph_task_create_send_message(&from_actor->ref, &to_actor_name, message);

    if (!task) {
        zend_throw_error(NULL, "Failed to serialise the message");
        return;
    }

    // @todo For now, we just make the main thread send all of the messages
    // In future, we could create a couple of specialised threads for
    // sending messages only (simplifying task handling for threads)
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + PHACTOR_G(actor_system)->thread_count;

    pthread_mutex_lock(&thread->tasks.lock);
    ph_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->tasks.lock);
}

ZEND_BEGIN_ARG_INFO(Actor_remove_arginfo, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Actor, remove)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    ph_actor_mark_for_removal(ph_actor_retrieve_from_zval(getThis()));
}

ZEND_BEGIN_ARG_INFO(Actor_abstract_receiveblock_arginfo, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Actor, receiveBlock)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    receive_block(getThis(), return_value);
}

ZEND_BEGIN_ARG_INFO(Actor_abstract_receive_arginfo, 0)
    ZEND_ARG_INFO(0, sender)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

zend_function_entry Actor_methods[] = {
    PHP_ME(Actor, send, Actor_send_arginfo, ZEND_ACC_PROTECTED)
    PHP_ME(Actor, remove, Actor_remove_arginfo, ZEND_ACC_PUBLIC)
    PHP_ABSTRACT_ME(Actor, receive, Actor_abstract_receive_arginfo)
    PHP_ME(Actor, receiveBlock, Actor_abstract_receiveblock_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void ph_actor_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "Actor", Actor_methods);
    Actor_ce = zend_register_internal_class(&ce);
    Actor_ce->ce_flags |= ZEND_ACC_ABSTRACT;
    Actor_ce->create_object = phactor_actor_ctor;

    memcpy(&phactor_actor_handlers, zh, sizeof(zend_object_handlers));

    phactor_actor_handlers.offset = XtOffsetOf(ph_actor_t, obj);
    phactor_actor_handlers.dtor_obj = ph_actor_dtor_object_dummy;
    phactor_actor_handlers.free_obj = ph_actor_free_object_dummy;
}
