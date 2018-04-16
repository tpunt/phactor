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

#include <pthread.h>

#include <main/php.h>

#include <uv.h>

#include "src/ph_context.h"
#include "src/ds/ph_hashtable.h"
#include "src/ds/ph_vector.h"
#include "src/ds/ph_queue.h"
#include "src/classes/actor.h"

#define ASYNC_THREAD_COUNT 10

typedef struct _ph_thread_t {
    pthread_t pthread;
    zend_ulong id; // local storage ID used to fetch local storage data
    ph_queue_t tasks;
    int offset;
    void*** ls; // pointer to local storage in TSRM
    ph_context_t context;
    ph_vector_t actor_removals;
    uv_loop_t event_loop;
} ph_thread_t;

typedef struct _ph_actor_system_t {
    // char system_reference[10]; // @todo needed when remote actors are introduced
    zend_bool initialised;
    zend_bool shutdown;
    ph_hashtable_t actors_by_ref;
    ph_hashtable_t actors_by_name; // ignore this HT mutex lock - use the actors_by_ref HT mutex lock instead
    int thread_count;
    int prepared_thread_count;
    int finished_thread_count;
    ph_thread_t *worker_threads;
    pthread_mutex_t lock;
    zend_object obj;
} ph_actor_system_t;

extern ph_thread_t main_thread;

void ph_actor_system_ce_init(void);

#endif
