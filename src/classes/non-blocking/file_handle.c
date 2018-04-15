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
#include "src/ph_message.h"
#include "src/classes/actor_system.h"
#include "src/classes/non-blocking/file_handle.h"

extern ph_actor_system_t *actor_system;
extern __thread int thread_offset;

zend_object_handlers ph_FileHandle_handlers;
zend_class_entry *ph_FileHandle_ce;

ph_file_handle_t *ph_file_handle_retrieve_from_object(zend_object *file_handle_obj)
{
    return (ph_file_handle_t *)((char *)file_handle_obj - file_handle_obj->handlers->offset);
}

void ph_open_file(uv_fs_t* req)
{
    if (req->result < 0) { // http://www-numi.fnal.gov/offline_software/srt_public_context/WebDocs/Errors/unix_system_errors.html
        switch (req->result) {
            case -2:
                zend_throw_exception_ex(NULL, 0, "Cannot open file because it does not exist.");
                break;
            default:
                zend_throw_exception_ex(NULL, 0, "Cannot open file because UNKNOWN (%d).", req->result);
        }
    } else {
        // file_handler_t *fh = (file_handler_t *) req;

        // uv_pipe_init(el->loop, &fh->file_pipe, 0);
        // uv_pipe_open(&fh->file_pipe, req->result);

        // uv_read_start((uv_stream_t *) &fh->file_pipe, alloc_buffer, fs_read_cb);
    }

    ph_file_handle_t *fh = (ph_file_handle_t *)req;

    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    ph_actor_t *actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &fh->actor_ref);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    // assert(actor->thread_offset == thread_offset);

    // There's no race condition here, since only the thread that created the
    // actor can free it
    if (actor) {
        ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + actor->thread_offset;

        pthread_mutex_lock(&thread->tasks.lock);
        ph_queue_push(&thread->tasks, ph_task_create_resume_actor(actor));
        pthread_mutex_unlock(&thread->tasks.lock);
    }
}

ZEND_BEGIN_ARG_INFO_EX(FileHandle___construct_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, fileName)
ZEND_END_ARG_INFO()

PHP_METHOD(FileHandle, __construct)
{
    ph_actor_t *actor = PHACTOR_ZG(currently_executing_actor);
    char *filename;
    size_t length;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(filename, length)
    ZEND_PARSE_PARAMETERS_END();

    ph_file_handle_t *fh = ph_file_handle_retrieve_from_object(Z_OBJ_P(getThis()));
    ph_thread_t *thread = PHACTOR_G(actor_system)->worker_threads + actor->thread_offset;

    // @todo check for NUL byte character (file name is not NUL safe)

    fh->name = filename;

    uv_fs_open(&thread->event_loop, (uv_fs_t *)fh, fh->name, O_ASYNC, 0, ph_open_file); // flags = unix only?

    pthread_mutex_lock(&actor->lock);

    if (actor->state == PH_ACTOR_ACTIVE) {
        actor->state = PH_ACTOR_BLOCKING;

        pthread_mutex_unlock(&actor->lock);

        ph_vmcontext_swap(&actor->internal->context.vmc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.vmc);

#ifdef PH_FIXED_STACK_SIZE
        ph_mcontext_swap(&actor->internal->context.mc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc);
#else
        ph_mcontext_interrupt(&actor->internal->context.mc, &PHACTOR_G(actor_system)->worker_threads[thread_offset].context.mc);
#endif
    } else {
        pthread_mutex_unlock(&actor->lock);
    }
}

zend_object* ph_file_handle_ctor(zend_class_entry *entry)
{
    ph_file_handle_t *fh = ecalloc(1, sizeof(ph_file_handle_t) + zend_object_properties_size(entry));

    zend_object_std_init(&fh->obj, entry);
    object_properties_init(&fh->obj, entry);

    fh->obj.handlers = &ph_FileHandle_handlers;

    ph_str_copy(&fh->actor_ref, PHACTOR_ZG(currently_executing_actor)->ref);

    return &fh->obj;
}

zend_function_entry FileHandle_methods[] = {
    PHP_ME(FileHandle, __construct, FileHandle___construct_arginfo, ZEND_ACC_PUBLIC)
    // PHP_ME(FileHandle, read, FileHandle_read_arginfo, ZEND_ACC_PUBLIC)
    // PHP_ME(FileHandle, write, FileHandle_write_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void ph_file_handle_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "phactor\\FileHandle", FileHandle_methods);
    ph_FileHandle_ce = zend_register_internal_class(&ce);
    ph_FileHandle_ce->create_object = ph_file_handle_ctor;
    ph_FileHandle_ce->ce_flags |= ZEND_ACC_FINAL;

    memcpy(&ph_FileHandle_handlers, zh, sizeof(zend_object_handlers));

    ph_FileHandle_handlers.offset = XtOffsetOf(ph_file_handle_t, obj);
    // ph_FileHandle_handlers.read_property = ph_actor_ref_read_property;
    // ph_FileHandle_handlers.write_property = ph_actor_ref_write_property;
}
