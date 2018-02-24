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
#include "src/ph_debug.h"
#include "src/ds/ph_queue.h"
#include "src/classes/actor_system.h"

extern ph_actor_system_t *actor_system;
extern zend_class_entry *Actor_ce;

ph_named_actor_t *new_named_actor(void)
{
    ph_named_actor_t *named_actor = malloc(sizeof(ph_named_actor_t));

    named_actor->state = PH_NAMED_ACTOR_CONSTRUCTING;
    named_actor->perceived_used = 0;
    ph_hashtable_init(&named_actor->actors, 1, ph_actor_mark_for_removal);

    return named_actor;
}

zend_long spawn_new_actor(zend_string *name, zend_string *class, zval *args, int argc)
{
    zend_function *constructor;
    zval zobj;
    ph_string_t key, *key2;
    ph_named_actor_t *named_actor;

    if (!PHACTOR_G(actor_system)) {
        zend_throw_exception(zend_ce_error, "The ActorSystem class must first be instantiated", 0);
        return 0;
    }

    if (!zend_fetch_class_by_name(class, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION)) {
        return 0;
    }

    pthread_mutex_lock(&PHACTOR_G(actor_system)->named_actors.lock);

    ph_str_set(&key, ZSTR_VAL(name), ZSTR_LEN(name));

    named_actor = ph_hashtable_search(&PHACTOR_G(actor_system)->named_actors, &key);

    if (!named_actor) {
        key2 = malloc(sizeof(ph_string_t));

        PH_STRL_P(key2) = PH_STRL(key);
        PH_STRV_P(key2) = PH_STRV(key);

        named_actor = new_named_actor();

        ph_hashtable_insert(&PHACTOR_G(actor_system)->named_actors, key2, named_actor);
    } else {
        key2 = ph_hashtable_key_fetch(&PHACTOR_G(actor_system)->named_actors, &key);
        ph_str_value_free(&key);
    }

    pthread_mutex_lock(&named_actor->actors.lock);
    int new_count = ++named_actor->perceived_used;
    pthread_mutex_unlock(&named_actor->actors.lock);

    pthread_mutex_unlock(&PHACTOR_G(actor_system)->named_actors.lock);

    ph_task_t *task = ph_task_create_new_actor(key2, class, args, argc);

    if (!task) {
        return 0;
    }

    int thread_offset = php_mt_rand_range(0, PHACTOR_G(actor_system)->thread_count - 1);
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + thread_offset;

    pthread_mutex_lock(&thread->tasks.lock);
    ph_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->tasks.lock);

    return new_count;
}

ZEND_BEGIN_ARG_INFO_EX(spawn_arginfo, 0, 0, 2)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_INFO(0, class)
ZEND_END_ARG_INFO()

PHP_FUNCTION(spawn)
{
    zend_string *name;
    zend_class_entry *class = Actor_ce;
    zval *args;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(2, -1)
        Z_PARAM_STR(name)
        Z_PARAM_CLASS(class)
        Z_PARAM_VARIADIC('*', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    RETVAL_LONG(spawn_new_actor(name, class->name, args, argc));
}

ZEND_BEGIN_ARG_INFO_EX(remove_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, name)
    ZEND_ARG_INFO(0, count)
ZEND_END_ARG_INFO()

PHP_FUNCTION(remove)
{
    zend_string *name;
    zend_long count = -1;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STR(name)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(count)
    ZEND_PARSE_PARAMETERS_END();

    RETVAL_LONG(ph_named_actor_removal(name, count));
}

const zend_function_entry phactor_functions[] = {
    ZEND_NS_FE("phactor", spawn, spawn_arginfo)
    ZEND_NS_FE("phactor", remove, remove_arginfo)
    PHP_FE_END
};
