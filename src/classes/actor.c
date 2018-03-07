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

extern ph_actor_system_t *actor_system;
extern __thread ph_actor_t *currently_processing_actor;
extern __thread int thread_offset;
extern zend_class_entry *ph_ActorRef_ce;

zend_object_handlers ph_Actor_handlers;
zend_class_entry *ph_Actor_ce;

ph_actor_internal_t *ph_actor_internal_retrieve_from_object(zend_object *actor_obj)
{
    return (ph_actor_internal_t *)((char *)actor_obj - actor_obj->handlers->offset);
}

ph_actor_t *ph_actor_retrieve_from_object(zend_object *actor_obj)
{
    ph_string_t *ref = ph_actor_internal_retrieve_from_object(actor_obj)->ref;
    ph_actor_t *actor;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, ref);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    assert(actor);

    return actor;
}

ph_actor_t *ph_actor_retrieve_from_zval(zval *actor_zval_obj)
{
    return ph_actor_retrieve_from_object(Z_OBJ_P(actor_zval_obj));
}

void ph_actor_remove_from_table(void *actor_void)
{
    ph_actor_t *actor = actor_void;

    // we are already holding the actors_by_ref mutex lock (this function is
    // called from ph_actor_mark_for_removal(), which is always called by the
    // actors_by_ref HT deletion function)
    if (actor->name) {
        ph_hashtable_delete(&PHACTOR_G(actor_system)->actors_by_name, actor->name);
    }
    ph_hashtable_delete(&PHACTOR_G(actor_system)->actors_by_ref, actor->ref);
}

void ph_actor_mark_for_removal(void *actor_void)
{
    ph_actor_t *actor = actor_void;

    if (!actor->internal) {
        // this can happen when the actor system is shutting down, but new
        // actors are still being created (they never become fully created)
        // @todo should this be performed asynchronously?
        ph_actor_free(actor_void);
    } else {
        ph_vector_t *actor_removals = PHACTOR_G(actor_system)->actor_removals + actor->thread_offset;

        pthread_mutex_lock(&actor_removals->lock);
        ph_vector_push(actor_removals, actor);
        pthread_mutex_unlock(&actor_removals->lock);

        if (actor->supervision) {
            pthread_mutex_lock(&actor->lock);
            ph_hashtable_apply(&actor->supervision->workers, ph_actor_remove_from_table);
            ph_hashtable_clear(&actor->supervision->workers);
            pthread_mutex_unlock(&actor->lock);
        }
    }
}

static void ph_actor_dtor_object_dummy(zend_object *obj){}
static void ph_actor_free_object_dummy(zend_object *obj){}

static void ph_actor_dtor_object(zend_object *obj)
{
    zend_objects_destroy_object(obj);
    zend_object_std_dtor(obj);
}

void ph_actor_free_dummy(void *actor_void){}

void ph_actor_internal_free(ph_actor_internal_t *actor_internal)
{
    ph_vmcontext_t vmc;

    ph_vmcontext_swap(&vmc, &actor_internal->context.vmc);
    zend_vm_stack_destroy();
    ph_vmcontext_set(&vmc);

    ph_mcontext_free(&actor_internal->context.mc);

    // @todo free actor_internal->ref ?

    ph_actor_dtor_object(&actor_internal->obj); // @todo why here and not as an object handler?

    efree(actor_internal);
}

void ph_actor_free(void *actor_void)
{
    ph_actor_t *actor = actor_void;

    pthread_mutex_destroy(&actor->lock);

    if (actor->supervision) {
        ph_hashtable_destroy(&actor->supervision->workers);
    }

    ph_queue_destroy(&actor->mailbox);

    // @todo free actor->name ?

    // see comment in ph_actor_mark_for_removal
    if (actor->internal) {
        ph_actor_internal_free(actor->internal);
    }
}

int ph_valid_actor_arg(zval *to_actor, char *using_actor_name, ph_string_t *to_actor_name)
{
    if (Z_TYPE_P(to_actor) == IS_STRING) {
        ph_str_set(to_actor_name, Z_STRVAL_P(to_actor), Z_STRLEN_P(to_actor));
        *using_actor_name = 1;

        return 1;
    }

    if (Z_TYPE_P(to_actor) == IS_OBJECT && instanceof_function(Z_OBJCE_P(to_actor), ph_ActorRef_ce)) {
        zend_string *ref = zend_string_init(ZEND_STRL("ref"), 0);
        zval zref, *value;
        zend_class_entry *fake_scope = EG(fake_scope);

        ZVAL_STR(&zref, ref);

        // fake the scope so that we can fetch the private property
        EG(fake_scope) = ph_ActorRef_ce;

        value = std_object_handlers.read_property(to_actor, &zref, BP_VAR_IS, NULL, NULL);

        EG(fake_scope) = fake_scope;

        zend_string_free(ref);

        ph_str_set(to_actor_name, Z_STRVAL_P(value), Z_STRLEN_P(value));
        *using_actor_name = 0;

        return 1;
    }

    return 0;
}

ph_actor_t *ph_actor_create(ph_string_t *name, ph_string_t *ref, ph_string_t *class_name, ph_entry_t *ctor_args, int ctor_argc)
{
    ph_actor_t *new_actor = calloc(1, sizeof(ph_actor_t));

    new_actor->name = name;
    new_actor->ref = ref;
    new_actor->state = PH_ACTOR_SPAWNING;
    new_actor->class_name = *class_name;
    new_actor->ctor_args = ctor_args;
    new_actor->ctor_argc = ctor_argc;
    new_actor->restart_count_streak = 0;

    ph_queue_init(&new_actor->mailbox, ph_msg_free);
    pthread_mutex_init(&new_actor->lock, NULL);

    return new_actor;
}

static void receive_block(ph_actor_t *actor, zval *return_value)
{
    pthread_mutex_lock(&actor->lock);
    if (actor->state == PH_ACTOR_SPAWNING) {
        pthread_mutex_unlock(&actor->lock);
        zend_throw_exception(zend_ce_exception, "Actor::receiveBlock() cannot be used in the constructor", 0);
        return;
    }

    actor->state = PH_ACTOR_IDLE;
    actor->restart_count_streak = 0;

    // @todo possible optimisation: if task queue is empty, just skip the next 7 lines
    if (ph_queue_size(&actor->mailbox)) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + actor->thread_offset;
        ph_task_t *task = ph_task_create_resume_actor(actor);

        pthread_mutex_lock(&thread->tasks.lock);
        ph_queue_push(&thread->tasks, task);
        pthread_mutex_unlock(&thread->tasks.lock);
    }
    pthread_mutex_unlock(&actor->lock);

    ph_vmcontext_swap(&actor->internal->context.vmc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.vmc);

#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_swap(&actor->internal->context.mc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc);
#else
    ph_mcontext_interrupt(&actor->internal->context.mc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc);
    // ph_mcontext_swap(&actor->internal->context.mc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc, 1);
#endif

    pthread_mutex_lock(&actor->lock);
    ph_message_t *message = ph_queue_pop(&actor->mailbox);
    actor->state = PH_ACTOR_ACTIVE;
    pthread_mutex_unlock(&actor->lock);

    ph_entry_convert_to_zval(return_value, message->message);
    ph_msg_free(message);
}

static zend_execute_data dummy_execute_data;

void process_message_handler(void)
{
    ph_actor_t *actor = currently_processing_actor;
    zend_object *object = &actor->internal->obj; // from tls
    zend_function *receive_function;
    zend_fcall_info fci;
    zval retval;
    int result;

    pthread_mutex_lock(&actor->lock);
    actor->state = PH_ACTOR_ACTIVE;
    pthread_mutex_unlock(&actor->lock);

    fci.size = sizeof(fci);
    fci.object = object;
    fci.retval = &retval;
    fci.param_count = 0;
    fci.params = NULL;
    fci.no_separation = 1;
    ZVAL_STRINGL(&fci.function_name, "receive", sizeof("receive")-1);

    EG(current_execute_data) = &dummy_execute_data;

    result = zend_call_function(&fci, NULL);

    EG(current_execute_data) = NULL;

    if (result == FAILURE && !EG(exception)) {
        zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(object->ce->name), "::", "receive");
    }

    zval_dtor(&fci.function_name);
    zval_ptr_dtor(&retval);

    if (EG(exception)) {
        // @todo save exception message to allow for crash logging?
        // Also, this probably needs special freeing
        EG(exception) = NULL;

        if (actor->supervisor) {
            ph_supervisor_handle_crash(actor->supervisor, actor);
            goto end;
        }
    }

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    if (actor->name) {
        ph_hashtable_delete(&PHACTOR_G(actor_system)->actors_by_name, actor->name);
    }
    ph_hashtable_delete(&PHACTOR_G(actor_system)->actors_by_ref, actor->internal->ref);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

end:;
#ifdef PH_FIXED_STACK_SIZE
    ph_mcontext_set(&PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc);
#endif
}

zend_object* phactor_actor_ctor(zend_class_entry *entry)
{
    ph_actor_internal_t *actor_internal = ecalloc(1, sizeof(ph_actor_internal_t) + zend_object_properties_size(entry));

    zend_object_std_init(&actor_internal->obj, entry);
    object_properties_init(&actor_internal->obj, entry);

    actor_internal->obj.handlers = &ph_Actor_handlers;

    if (!PHACTOR_ZG(allowed_to_construct_object)) {
        zend_throw_exception(zend_ce_error, "Actors cannot be created via class instantiation - create an ActorRef object instead", 0);
        return &actor_internal->obj;
    }

    ph_mcontext_init(&actor_internal->context.mc, process_message_handler);

    zend_vm_stack_init();
    ph_vmcontext_get(&actor_internal->context.vmc);

    /*
    Prevents an actor from being destroyed automatically.
    */
    ++GC_REFCOUNT(&actor_internal->obj); // @todo necessary still?

    return &actor_internal->obj;
}

ZEND_BEGIN_ARG_INFO_EX(Actor_send_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, actor)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

PHP_METHOD(Actor, send)
{
    ph_string_t from_actor_ref, to_actor_name;
    char using_actor_name;
    zval *to_actor, *message;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &to_actor, &message) != SUCCESS) {
        return;
    }

    if (!ph_valid_actor_arg(to_actor, &using_actor_name, &to_actor_name)) {
        zend_throw_exception(NULL, "Invalid recipient value", 0);
        return;
    }

    ph_actor_t *from_actor = ph_actor_retrieve_from_object(Z_OBJ(EX(This)));

    ph_str_set(&from_actor_ref, PH_STRV_P(from_actor->internal->ref), PH_STRL_P(from_actor->internal->ref));

    ph_task_t *task = ph_task_create_send_message(&from_actor_ref, &to_actor_name, using_actor_name, message);

    if (!task) {
        ph_str_value_free(&from_actor_ref);
        ph_str_value_free(&to_actor_name);
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

ZEND_BEGIN_ARG_INFO(Actor_abstract_receiveblock_arginfo, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Actor, receiveBlock)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    receive_block(ph_actor_retrieve_from_object(Z_OBJ(EX(This))), return_value);
}

ZEND_BEGIN_ARG_INFO(Actor_abstract_receive_arginfo, 0)
ZEND_END_ARG_INFO()

zend_function_entry Actor_methods[] = {
    PHP_ME(Actor, send, Actor_send_arginfo, ZEND_ACC_PROTECTED | ZEND_ACC_FINAL)
    PHP_ABSTRACT_ME(Actor, receive, Actor_abstract_receive_arginfo)
    PHP_ME(Actor, receiveBlock, Actor_abstract_receiveblock_arginfo, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
    PHP_FE_END
};

void ph_actor_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\Actor", Actor_methods);
    ph_Actor_ce = zend_register_internal_class(&ce);
    ph_Actor_ce->ce_flags |= ZEND_ACC_ABSTRACT;
    ph_Actor_ce->create_object = phactor_actor_ctor;

    memcpy(&ph_Actor_handlers, zh, sizeof(zend_object_handlers));

    ph_Actor_handlers.offset = XtOffsetOf(ph_actor_internal_t, obj);
    ph_Actor_handlers.dtor_obj = ph_actor_dtor_object_dummy;
    ph_Actor_handlers.free_obj = ph_actor_free_object_dummy;
}
