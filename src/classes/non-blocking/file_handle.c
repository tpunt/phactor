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

zend_object_handlers ph_FileHandle_handlers;
zend_class_entry *ph_FileHandle_ce;

ph_file_handle_t *ph_file_handle_retrieve_from_object(zend_object *file_handle_obj)
{
    return (ph_file_handle_t *)((char *)file_handle_obj - file_handle_obj->handlers->offset);
}

void ph_fetch_and_reschedule_actor(ph_file_handle_t *fh)
{
    pthread_mutex_lock(&PHACTOR_G(actor_system)->actors_by_ref.lock);
    ph_actor_t *actor = ph_hashtable_search(&PHACTOR_G(actor_system)->actors_by_ref, &fh->actor_ref);
    pthread_mutex_unlock(&PHACTOR_G(actor_system)->actors_by_ref.lock);

    // There's no race condition here, since only the thread that created the
    // actor can free it
    if (actor && actor->state == PH_ACTOR_BLOCKING && fh->actor_restart_count == actor->restart_count) {
        pthread_mutex_lock(&PHACTOR_ZG(ph_thread)->tasks.lock);
        ph_queue_push(&PHACTOR_ZG(ph_thread)->tasks, ph_task_create_resume_actor(actor));
        pthread_mutex_unlock(&PHACTOR_ZG(ph_thread)->tasks.lock);
    }
}

void ph_file_open(uv_fs_t* req)
{
    ph_file_handle_t *fh = (ph_file_handle_t *)req;

    if (req->result < 0) { // http://www-numi.fnal.gov/offline_software/srt_public_context/WebDocs/Errors/unix_system_errors.html
        switch (req->result) {
            case -2:
                zend_throw_exception_ex(NULL, 0, "Cannot open file because it does not exist");
                break;
            default:
                zend_throw_exception_ex(NULL, 0, "Cannot open file because UNKNOWN (%d)", req->result);
        }
    } else {
        fh->fd = req->result;
    }

    ph_fetch_and_reschedule_actor(fh);
}

void ph_file_stat(uv_fs_t* req)
{
    ph_file_handle_t *fh = (ph_file_handle_t *)req;

    if (req->result < 0) {
        zend_throw_exception_ex(NULL, 0, "An error occurred when reading the file's stats (%d)", req->result);
    } else {
        fh->file_size = req->statbuf.st_size;
    }

    ph_fetch_and_reschedule_actor(fh);
}

void ph_file_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    ph_file_handle_t *fh = (ph_file_handle_t *) ((char *) stream - offsetof(ph_file_handle_t, file_pipe));

    if (nread < 0) {
        if (nread == UV_EOF) {
            uv_close((uv_handle_t *)&fh->file_pipe, NULL);
            uv_read_stop(stream);
        } else {
            zend_throw_exception_ex(NULL, 0, "Could not read file (%d)", nread);
        }
    }

    ph_fetch_and_reschedule_actor(fh);
}

void ph_file_write(uv_write_t *req, int status)
{
    ph_file_handle_t *fh = (ph_file_handle_t *)((char *)req - offsetof(ph_file_handle_t, write));

    if (status < 0) {
        zend_throw_exception_ex(NULL, 0, "Could not write to file (%d)", status);
    }

    ph_fetch_and_reschedule_actor(fh);
}

void ph_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	ph_file_handle_t *fh = (ph_file_handle_t *)((char *)handle - offsetof(ph_file_handle_t, file_pipe));

	*buf = uv_buf_init(fh->buffer, fh->buffer_size);
}

void ph_blocking_context_switch(ph_actor_t *actor)
{
    pthread_mutex_lock(&actor->lock);

    if (actor->state == PH_ACTOR_ACTIVE) {
        actor->state = PH_ACTOR_BLOCKING;

        pthread_mutex_unlock(&actor->lock);

        ph_vmcontext_swap(&actor->internal->context.vmc, &PHACTOR_ZG(ph_thread)->context.vmc);

#ifdef PH_FIXED_STACK_SIZE
        ph_mcontext_swap(&actor->internal->context.mc, &PHACTOR_ZG(ph_thread)->context.mc);
#else
        ph_mcontext_interrupt(&actor->internal->context.mc, &PHACTOR_ZG(ph_thread)->context.mc);
#endif

        pthread_mutex_lock(&actor->lock);

        actor->state = PH_ACTOR_ACTIVE;
    }

    pthread_mutex_unlock(&actor->lock);
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

    // @todo check for NUL byte character (file name is not NUL safe)

    fh->name = filename;
    fh->actor_restart_count = actor->restart_count;

    // O_ASYNC = unix only?
    if (!uv_fs_open(&PHACTOR_ZG(ph_thread)->event_loop, (uv_fs_t *)fh, fh->name, O_ASYNC, 0, ph_file_open)) {
        ph_blocking_context_switch(actor);
    } else {
        zend_throw_exception(NULL, "Failed to begin opening the file", 0);
    }
}

ZEND_BEGIN_ARG_INFO_EX(FileHandle_read_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(FileHandle, read)
{
    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    ph_file_handle_t *fh = ph_file_handle_retrieve_from_object(Z_OBJ_P(getThis()));
    ph_actor_t *actor = PHACTOR_ZG(currently_executing_actor);

    fh->actor_restart_count = actor->restart_count;

    uv_fs_stat(&PHACTOR_ZG(ph_thread)->event_loop, (uv_fs_t *)fh, fh->name, ph_file_stat);

    ph_blocking_context_switch(actor);

    if (EG(exception)) {
        return;
    }

    if (!fh->file_size) {
        RETURN_EMPTY_STRING();
    }

    fh->buffer_size = fh->file_size;
    fh->buffer = emalloc(fh->buffer_size);

    if (!uv_pipe_init(&PHACTOR_ZG(ph_thread)->event_loop, &fh->file_pipe, 0)) {
        if (!uv_pipe_open(&fh->file_pipe, fh->fd)) {
            if (!uv_read_start((uv_stream_t *)&fh->file_pipe, ph_alloc_buffer, ph_file_read)) {
                ph_blocking_context_switch(actor);

                if (!EG(exception)) {
                    RETVAL_NEW_STR(zend_string_init(fh->buffer, fh->buffer_size, 0));
                }
            } else {
                zend_throw_exception(NULL, "Failed to begin reading the file", 0);
            }
        } else {
            zend_throw_exception(NULL, "Failed to open the file pipe", 0);
        }
    } else {
        zend_throw_exception(NULL, "Failed to initialise the file pipe", 0);
    }

    efree(fh->buffer);
    fh->buffer_size = 0;
}

ZEND_BEGIN_ARG_INFO_EX(FileHandle_write_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, content)
ZEND_END_ARG_INFO()

PHP_METHOD(FileHandle, write)
{
    char *content;
    size_t length;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(content, length)
    ZEND_PARSE_PARAMETERS_END();

    ph_file_handle_t *fh = ph_file_handle_retrieve_from_object(Z_OBJ_P(getThis()));
    ph_actor_t *actor = PHACTOR_ZG(currently_executing_actor);
    uv_buf_t buffer[1];

    fh->actor_restart_count = actor->restart_count;
    fh->buffer_size = length;

    buffer[0].base = content;
    buffer[0].len = length;

    if (!uv_pipe_init(&PHACTOR_ZG(ph_thread)->event_loop, &fh->file_pipe, 0)) {
        if (!uv_pipe_open(&fh->file_pipe, fh->fd)) {
            if (!uv_write((uv_write_t *)&fh->write, (uv_stream_t *)&fh->file_pipe, buffer, 1, ph_file_write)) {
                ph_blocking_context_switch(actor);
            } else {
                zend_throw_exception(NULL, "Failed to begin writing to the file", 0);
            }
        } else {
            zend_throw_exception(NULL, "Failed to open the file pipe", 0);
        }
    } else {
        zend_throw_exception(NULL, "Failed to initialise the file pipe", 0);
    }
}

zend_object* ph_file_handle_ctor(zend_class_entry *entry)
{
    ph_file_handle_t *fh = ecalloc(1, sizeof(ph_file_handle_t) + zend_object_properties_size(entry));

    zend_object_std_init(&fh->obj, entry);
    object_properties_init(&fh->obj, entry);

    fh->obj.handlers = &ph_FileHandle_handlers;

    ph_str_copy(&fh->actor_ref, PHACTOR_ZG(currently_executing_actor)->ref);
    fh->file_size = -1;

    return &fh->obj;
}

zend_function_entry FileHandle_methods[] = {
    PHP_ME(FileHandle, __construct, FileHandle___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(FileHandle, read, FileHandle_read_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(FileHandle, write, FileHandle_write_arginfo, ZEND_ACC_PUBLIC)
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
