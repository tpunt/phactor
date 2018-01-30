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

#ifndef PHACTOR_H
#define PHACTOR_H

#include <Zend/zend_modules.h>

extern zend_module_entry phactor_module_entry;
#define phpext_phactor_ptr &phactor_module_entry

#define PHP_PHACTOR_VERSION "0.1.0"

#ifdef PHP_WIN32
#    define PHP_PHACTOR_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#    define PHP_PHACTOR_API __attribute__ ((visibility("default")))
#else
#    define PHP_PHACTOR_API
#endif

#include <main/php.h>
#include <main/php_globals.h>
#include <main/php_ini.h>
#include <main/php_main.h>
#include <main/php_network.h>
#include <main/php_ticks.h>

#include <TSRM/TSRM.h>

#include <Zend/zend.h>
#include <Zend/zend_closures.h>
#include <Zend/zend_compile.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_extensions.h>
#include <Zend/zend_globals.h>
#include <Zend/zend_hash.h>
#include <Zend/zend_inheritance.h>
#include <Zend/zend_interfaces.h>
#include <Zend/zend_list.h>
#include <Zend/zend_modules.h>
#include <Zend/zend_object_handlers.h>
#include <Zend/zend_smart_str.h>
#include <Zend/zend_ts_hash.h>
#include <Zend/zend_types.h>
#include <Zend/zend_variables.h>
#include <Zend/zend_vm.h>

#include <ext/standard/basic_functions.h>
#include <ext/standard/info.h>
#include <ext/standard/php_rand.h>
#include <ext/standard/php_var.h>

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

typedef struct _actor_t actor_t;

#include "ph_general.h"
#include "ph_prop_store.h"
#include "ph_context.h"

extern zend_class_entry *ActorSystem_ce;
extern zend_class_entry *Actor_ce;

#define PHACTOR_ZG(v) TSRMG(phactor_globals_id, zend_phactor_globals *, v)
// #define PHACTOR_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(phactor, v)
#define PHACTOR_G(v) v // phactor_globals.v

#if defined(COMPILE_DL_PHACTOR)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

//sysconf(_SC_NPROCESSORS_ONLN);
#define THREAD_COUNT 10


#define PHACTOR_CTX(ls, id, type, element) (((type) (*((void ***) ls))[TSRM_UNSHUFFLE_RSRC_ID(id)])->element)
#define PHACTOR_EG(ls, v) PHACTOR_CTX(ls, executor_globals_id, zend_executor_globals*, v)
#define PHACTOR_CG(ls, v) PHACTOR_CTX(ls, compiler_globals_id, zend_compiler_globals*, v)
#define PHACTOR_SG(ls, v) PHACTOR_CTX(ls, sapi_globals_id, sapi_globals_struct*, v)
// shortcut macros of PHACTOR_MAIN_EG et al.?

#define PH_THREAD_G(v) thread->v

typedef enum _task_type_t {
    PROCESS_MESSAGE_TASK,
    SEND_MESSAGE_TASK,
    RESUME_ACTOR_TASK,
    NEW_ACTOR_TASK
} task_type_t;

typedef enum _actor_state_t {
    NEW_STATE,   // not waiting - starts a fresh context
    IDLE_STATE,  // waiting for something - needs context restoring
    ACTIVE_STATE // in execution - prevents parallel execution of an actor
} actor_state_t;

typedef struct _message_t {
    ph_string_t from_actor_ref; // could just be a pointer - what about remote actors?
    entry_t *message; // why the separate allocation?
    struct _message_t *next_message;
} message_t;

struct _actor_t {
    ph_string_t ref;
    ph_string_t *name;
    message_t *mailbox;
    zend_execute_data *vm_stack; // vm_context
    zend_executor_globals eg;
    ph_context_t actor_context; // c_context
    actor_state_t state;
    entry_t *retval; // @todo make singly-linked list?
    int thread_offset;
    zend_object obj;
};

typedef struct _process_message_task {
    actor_t *for_actor;
} process_message_task;

typedef struct _send_message_task {
    ph_string_t from_actor_ref;
    ph_string_t to_actor_name;
    entry_t *message;
} send_message_task;

typedef struct _resume_actor_task {
    actor_t *actor;
} resume_actor_task;

typedef enum _named_actor_status_t {
    CONSTRUCTION,
    ACTIVE,
    DESTRUCTION // @todo needed?
} named_actor_status_t;

typedef struct _named_actor_t {
    // ph_string_t name; // needed?
    named_actor_status_t status;
	int perceived_used; // for the calling code
    ph_hashtable_t actors;
    // mutex lock?
} named_actor_t;

typedef struct new_actor_task_t {
    ph_string_t *named_actor_key;
    ph_string_t class_name;
    entry_t *args;
    int argc;
} new_actor_task_t;

typedef struct _task_t {
    union {
        process_message_task pmt;
        send_message_task smt;
        resume_actor_task rat;
        new_actor_task_t nat;
    } task;
    task_type_t task_type;
    struct _task_t *next_task;
} task_t;

typedef struct _thread_t {
    pthread_t thread; // must be first member
    zend_ulong id; // local storage ID used to fetch local storage data
    zend_executor_globals eg;
    task_t *tasks;
    pthread_mutex_t ph_task_mutex;
    int offset;
    void*** ls; // pointer to local storage in TSRM
    ph_context_t thread_context;
} thread_t;

typedef struct _actor_removal_t {
    actor_t **actors;
    int count;
    int used;
} actor_removal_t;

typedef struct _actor_system_t {
    // char system_reference[10]; // @todo needed when remote actors are introduced
    zend_bool initialised;
    ph_hashtable_t actors;
    ph_hashtable_t named_actors;
    int thread_count;
    int prepared_thread_count;
    int finished_thread_count;
    thread_t *worker_threads;
    actor_removal_t *actor_removals;
    zend_bool daemonised_actor_system;
    zend_object obj;
} actor_system_t;


extern thread_t main_thread;
extern zend_object_handlers phactor_actor_handlers;
extern zend_object_handlers phactor_actor_system_handlers;

ZEND_EXTERN_MODULE_GLOBALS(phactor)

ZEND_BEGIN_MODULE_GLOBALS(phactor)
    HashTable op_array_file_names;
ZEND_END_MODULE_GLOBALS(phactor)

#endif


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
