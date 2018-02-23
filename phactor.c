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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <main/php.h>
#include <main/SAPI.h>
#include <ext/standard/info.h>

#include "php_phactor.h"
#include "src/ph_functions.h"
#include "src/classes/actor_system.h"
#include "src/classes/actor.h"

#ifndef ZTS
# error "Zend Thread Safety (ZTS) mode is required"
#endif

#if !defined(ZEND_ENABLE_STATIC_TSRMLS_CACHE) || !ZEND_ENABLE_STATIC_TSRMLS_CACHE
# error "TSRMLS static cache is required"
#endif

extern pthread_mutex_t ph_named_actors_mutex;
extern pthread_mutex_t global_actor_id_lock;

ZEND_DECLARE_MODULE_GLOBALS(phactor)

PHP_MINIT_FUNCTION(phactor)
{
    actor_system_ce_init();
    ph_actor_ce_init();

    pthread_mutex_init(&ph_named_actors_mutex, NULL);
    pthread_mutex_init(&global_actor_id_lock, NULL);

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(phactor)
{
    pthread_mutex_destroy(&ph_named_actors_mutex);
    pthread_mutex_destroy(&global_actor_id_lock);

    return SUCCESS;
}

PHP_RINIT_FUNCTION(phactor)
{
    TSRMLS_CACHE_UPDATE();

    zend_hash_init(&PHACTOR_ZG(op_array_file_names), 8, NULL, ZVAL_PTR_DTOR, 0);
    PHACTOR_ZG(allowed_to_construct_object) = 0;

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(phactor)
{
    zend_hash_destroy(&PHACTOR_ZG(op_array_file_names));

    return SUCCESS;
}

PHP_MINFO_FUNCTION(phactor)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "phactor support", "enabled");
    php_info_print_table_end();
}

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
    NULL,
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_PHACTOR
TSRMLS_CACHE_DEFINE()
ZEND_GET_MODULE(phactor)
#endif
