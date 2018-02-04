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

#ifndef PH_COMMON_H
#define PH_COMMON_H

#include <pthread.h>

#include <main/php.h>

#include "src/ph_task.h"
#include "src/ds/ph_queue.h"
#include "src/ph_context.h"
#include "src/ph_string.h"
#include "src/ph_entry.h"

typedef struct _ph_task_t ph_task_t;
typedef struct _ph_entry_t ph_entry_t;

typedef struct _ph_thread_t {
    pthread_t pthread; // must be first member
    zend_ulong id; // local storage ID used to fetch local storage data
    zend_executor_globals eg;
    ph_queue_t tasks;
    pthread_mutex_t ph_task_mutex;
    int offset;
    void*** ls; // pointer to local storage in TSRM
    ph_context_t thread_context;
} ph_thread_t;

static __thread ph_thread_t *thread;
static pthread_mutex_t phactor_actors_mutex;
static pthread_mutex_t phactor_named_actors_mutex;

#define PH_THREAD_G(v) thread->v

#endif
