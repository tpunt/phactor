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

#ifndef PH_ACTOR_SYSTEM_H
#define PH_ACTOR_SYSTEM_H

#include "src/ds/ph_hashtable.h"
#include "src/ds/ph_vector.h"
#include "src/classes/actor.h"
#include "src/classes/common.h"

//sysconf(_SC_NPROCESSORS_ONLN);
#define THREAD_COUNT 10

typedef struct _ph_actor_system_t {
    // char system_reference[10]; // @todo needed when remote actors are introduced
    zend_bool initialised;
    ph_hashtable_t actors;
    ph_hashtable_t named_actors;
    int thread_count;
    int prepared_thread_count;
    int finished_thread_count;
    ph_thread_t *worker_threads;
    ph_vector_t *actor_removals;
    zend_bool daemonised;
    zend_object obj;
} ph_actor_system_t;

extern ph_thread_t main_thread;
extern pthread_mutex_t phactor_mutex;

void actor_system_ce_init(void);

#endif
