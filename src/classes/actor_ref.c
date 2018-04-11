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

void ph_actor_ref_create(zval *zobj, zend_string *actor_class, zval *ctor_args, zend_string *actor_name, ph_string_t *supervisor_ref)
{
    if (!PHACTOR_G(actor_system)) {
        zend_throw_error(NULL, "The ActorSystem class must first be instantiated", 0);
        return;
    }

    ph_entry_t *new_ctor_args = NULL;
    int new_ctor_argc = 0;

    if (ctor_args && Z_ARR_P(ctor_args)->nNumUsed) {
        zval *value;
        int i = 0;

        new_ctor_args = malloc(sizeof(ph_entry_t) * Z_ARR_P(ctor_args)->nNumUsed);
        new_ctor_argc = Z_ARR_P(ctor_args)->nNumUsed;

        ZEND_HASH_FOREACH_VAL(Z_ARR_P(ctor_args), value) {
            if (ph_entry_convert_from_zval(new_ctor_args + i, value)) {
                ++i;
            } else {
                zend_throw_error(NULL, "Failed to serialise argument %d of the constructor arguments", i + 1);

                for (int i2 = 0; i2 < i; ++i2) {
                    ph_entry_value_free(new_ctor_args + i2);
                }

                free(new_ctor_args);

                return;
            }
        } ZEND_HASH_FOREACH_END();
    }

    ph_string_t *new_actor_ref = ph_str_alloc(ACTOR_REF_LEN);
    ph_string_t new_actor_class;
    ph_string_t *new_actor_name = NULL;

    ph_actor_ref_set(new_actor_ref);
    ph_str_set(&new_actor_class, ZSTR_VAL(actor_class), ZSTR_LEN(actor_class));

    if (actor_name) {
        new_actor_name = ph_str_create(ZSTR_VAL(actor_name), ZSTR_LEN(actor_name));
    }

    ph_task_t *task = ph_task_create_new_actor(new_actor_ref, &new_actor_class);
    ph_actor_t *new_actor = ph_actor_create(new_actor_name, new_actor_ref, &new_actor_class, new_ctor_args, new_ctor_argc);
    ph_actor_t *supervisor = NULL;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    if (supervisor_ref) {
        supervisor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, supervisor_ref);

        if (!supervisor) { // supervisor has died already - abort (silently)
            pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

            // @todo logging?

            ph_task_free(task);
            ph_actor_free(new_actor);

            return;
        }
    }

    if (actor_name) {
        if (ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_name, new_actor_name)) {
            pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

            zend_throw_error(NULL, "An actor with the specified name has already been created", 0);

            ph_task_free(task);
            ph_actor_free(new_actor);

            return;
        }

        ph_hashtable_insert(&PHACTOR_G(actor_system)->actors_by_name, new_actor_name, new_actor);
    }

    if (supervisor) {
        ph_supervisor_add_worker(supervisor, new_actor);
    }

    ph_hashtable_insert(&PHACTOR_G(actor_system)->actors_by_ref, new_actor_ref, new_actor);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    int thread_offset = php_mt_rand_range(0, PHACTOR_G(actor_system)->thread_count - 1);
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + thread_offset;

    new_actor->thread_offset = thread_offset;

    pthread_mutex_lock(&thread->tasks.lock);
    ph_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->tasks.lock);

    zend_class_entry *fake_scope;

    if (supervisor_ref) { // we are not using ActorRef::__construct
        fake_scope = EG(fake_scope);
        EG(fake_scope) = ph_ActorRef_ce;
    }

    zval zref, value;

    ZVAL_INTERNED_STR(&zref, common_strings.ref);
    ZVAL_STRINGL(&value, PH_STRV_P(new_actor_ref), PH_STRL_P(new_actor_ref));

    zend_std_write_property(zobj, &zref, &value, NULL);
    zend_string_release(Z_STR(value));

    if (actor_name) {
        zval zname, value;

        ZVAL_INTERNED_STR(&zname, common_strings.name);
        ZVAL_STR(&value, actor_name);

        zend_std_write_property(zobj, &zname, &value, NULL);
        zend_string_release(actor_name); // @todo needed?
    }

    if (supervisor_ref) { // we are not using ActorRef::__construct
        EG(fake_scope) = fake_scope;
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

    ph_actor_ref_create(getThis(), actor_class->name, ctor_args, actor_name, NULL);
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef_get_ref_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, getRef)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    zval zref, *value;

    ZVAL_INTERNED_STR(&zref, common_strings.ref);

    value = std_object_handlers.read_property(getThis(), &zref, BP_VAR_IS, NULL, NULL);

    *return_value = *value;
}

ZEND_BEGIN_ARG_INFO_EX(ActorRef_get_name_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ActorRef, getName)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    zval zname, *value;

    ZVAL_INTERNED_STR(&zname, common_strings.name);

    value = std_object_handlers.read_property(getThis(), &zname, BP_VAR_IS, NULL, NULL);

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
        zend_throw_exception(NULL, "Failed to create an ActorRef object from the given Actor class", 0);
    } else {
        ph_actor_t *actor = ph_actor_retrieve_from_zval(actor_obj);
        zval zref, value;

        ZVAL_INTERNED_STR(&zref, common_strings.ref);
        ZVAL_STRINGL(&value, PH_STRV_P(actor->internal->ref), PH_STRL_P(actor->internal->ref));

        zend_std_write_property(&zobj, &zref, &value, NULL);
        // zend_string_release(Z_STR(value));

        if (actor->name) {
            zval zname, value;

            ZVAL_INTERNED_STR(&zname, common_strings.name);
            ZVAL_STRINGL(&value, PH_STRV_P(actor->name), PH_STRL_P(actor->name));

            zend_std_write_property(&zobj, &zname, &value, NULL);
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
