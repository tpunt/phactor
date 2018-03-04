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
#include "src/ph_string.h"
#include "src/ph_task.h"
#include "src/classes/actor_system.h"

extern ph_actor_system_t *actor_system;
extern zend_class_entry *ph_Actor_ce;

#define ACTOR_REF_LEN 33

zend_object_handlers ph_ActorRef_handlers;
zend_class_entry *ph_ActorRef_ce;

pthread_mutex_t global_actor_id_lock;
int global_actor_id;

// @todo actually generate UUIDs for remote actors
static void ph_actor_ref_set(ph_string_t *ref)
{
    pthread_mutex_lock(&global_actor_id_lock);
    sprintf(ref->val, "%022d%010d", 0, ++global_actor_id);
    pthread_mutex_unlock(&global_actor_id_lock);
}

zend_object *ph_actor_ref_ctor(zend_class_entry *entry)
{
    zend_object *obj = ecalloc(1, sizeof(zend_object) + zend_object_properties_size(entry));

    zend_object_std_init(obj, entry);
    object_properties_init(obj, entry);

    obj->handlers = &ph_ActorRef_handlers;

    return obj;
}

zval *ph_actor_ref_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_error(zend_ce_error, "Properties on ActorRef objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void ph_actor_ref_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_error(zend_ce_error, "Properties on ActorRef objects are not enabled", 0);
}

void ph_actor_ref_create(zval *zobj, zend_string *actor_class, zval *ctor_args, zend_string *actor_name)
{
    if (!PHACTOR_G(actor_system)) {
        zend_throw_exception(zend_ce_error, "The ActorSystem class must first be instantiated", 0);
        return;
    }

    ph_task_t *task = ph_task_create_new_actor(actor_class, ctor_args);

    if (!task) {
        zend_throw_exception(zend_ce_error, "Failed to serialise the constructor arguments", 0);
        return;
    }

    // set the ref here, rather than before ph_task_create_new_actor in case it fails
    task->u.nat.actor_ref = ph_str_alloc(ACTOR_REF_LEN);
    ph_actor_ref_set(task->u.nat.actor_ref);

    ph_string_t *new_actor_name = NULL;

    if (actor_name) {
        new_actor_name = ph_str_create(ZSTR_VAL(actor_name), ZSTR_LEN(actor_name));
    }

    ph_actor_t *new_actor = ph_actor_create(new_actor_name);

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    if (actor_name) {
        if (ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_name, new_actor_name)) {
            zend_throw_exception(zend_ce_error, "An actor with the specified name has already been created", 0);
            ph_str_free(task->u.nat.actor_ref);
            ph_str_free(new_actor_name);
            ph_task_free(task);
            return;
        }

        ph_hashtable_insert(&PHACTOR_G(actor_system)->actors_by_name, new_actor_name, new_actor);
    }

    ph_hashtable_insert(&PHACTOR_G(actor_system)->actors_by_ref, task->u.nat.actor_ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    int thread_offset = php_mt_rand_range(0, PHACTOR_G(actor_system)->thread_count - 1);
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + thread_offset;

    pthread_mutex_lock(&thread->tasks.lock);
    ph_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->tasks.lock);

    zend_string *ref = zend_string_init(ZEND_STRL("ref"), 0);
    zval zref, value;
    ZVAL_STR(&zref, ref);
    ZVAL_STRINGL(&value, PH_STRV_P(task->u.nat.actor_ref), PH_STRL_P(task->u.nat.actor_ref));

    zend_std_write_property(zobj, &zref, &value, NULL);
    zend_string_free(ref);
    zend_string_release(Z_STR(value));

    if (actor_name) {
        zend_string *name = zend_string_init(ZEND_STRL("name"), 0);
        zval zname, value;
        ZVAL_STR(&zname, name);
        ZVAL_STR(&value, actor_name);

        zend_std_write_property(zobj, &zname, &value, NULL);
        zend_string_free(name);
        zend_string_release(actor_name); // @todo needed?
    }
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef___construct_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, actorClass)
    ZEND_ARG_INFO(0, ctorArgs)
    ZEND_ARG_INFO(0, actorName)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, __construct)
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

    ph_actor_ref_create(getThis(), actor_class->name, ctor_args, actor_name);
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef_get_ref_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, getRef)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    zend_string *ref = zend_string_init(ZEND_STRL("ref"), 0);
    zval zref, *value;
    ZVAL_STR(&zref, ref);

    value = std_object_handlers.read_property(getThis(), &zref, BP_VAR_IS, NULL, NULL);
    zend_string_free(ref);

    *return_value = *value;
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef_get_name_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, getName)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    zend_string *ref = zend_string_init(ZEND_STRL("name"), 0);
    zval zref, *value;
    ZVAL_STR(&zref, ref);

    value = std_object_handlers.read_property(getThis(), &zref, BP_VAR_IS, NULL, NULL);
    zend_string_free(ref);

    *return_value = *value;
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef_from_actor_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, actor)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, fromActor)
{
    zval *actor_obj, zobj;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(actor_obj, ph_Actor_ce)
    ZEND_PARSE_PARAMETERS_END();

    if (object_init_ex(&zobj, ph_ActorRef_ce) != SUCCESS) {
        zend_throw_exception(zend_ce_exception, "Failed to create an ActorRef object from the given Actor class", 0);
    } else {
        ph_actor_t *actor = ph_actor_retrieve_from_zval(actor_obj);
        zend_string *ref = zend_string_init(ZEND_STRL("ref"), 0);
        zval zref, value;

        ZVAL_STR(&zref, ref);
        ZVAL_STRINGL(&value, PH_STRV_P(actor->internal->ref), PH_STRL_P(actor->internal->ref));

        zend_std_write_property(&zobj, &zref, &value, NULL);
        zend_string_free(ref);
        // zend_string_release(Z_STR(value));

        if (actor->name) {
            zend_string *name = zend_string_init(ZEND_STRL("name"), 0);
            zval zname, value;
            ZVAL_STR(&zname, name);
            ZVAL_STRINGL(&value, PH_STRV_P(actor->name), PH_STRL_P(actor->name));

            zend_std_write_property(&zobj, &zname, &value, NULL);
            zend_string_free(name);
            // zend_string_release(Z_STR(value));
        }

        ZVAL_OBJ(return_value, Z_OBJ(zobj));
    }
}

zend_function_entry ActorRef_methods[] = {
    PHP_ME(ActorRef, __construct, ActorRef___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ActorRef, getRef, ActorRef_get_ref_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ActorRef, getName, ActorRef_get_name_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ActorRef, fromActor, ActorRef_from_actor_arginfo, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

void ph_actor_ref_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\ActorRef", ActorRef_methods);
    ph_ActorRef_ce = zend_register_internal_class(&ce);
    ph_ActorRef_ce->create_object = ph_actor_ref_ctor;
    ph_ActorRef_ce->ce_flags |= ZEND_ACC_FINAL;

    memcpy(&ph_ActorRef_handlers, zh, sizeof(zend_object_handlers));

    ph_ActorRef_handlers.read_property = ph_actor_ref_read_property;
    ph_ActorRef_handlers.write_property = ph_actor_ref_write_property;

    zend_declare_property_string(ph_ActorRef_ce, ZEND_STRL("ref"), "", ZEND_ACC_PROTECTED);
    Z_TYPE_INFO_P(ph_ActorRef_ce->default_properties_table) = IS_INTERNED_STRING_EX; // RCs on default property values??
    zend_declare_property_string(ph_ActorRef_ce, ZEND_STRL("name"), "", ZEND_ACC_PROTECTED);
    Z_TYPE_INFO_P(ph_ActorRef_ce->default_properties_table + 1) = IS_INTERNED_STRING_EX; // RCs on default property values??
}
