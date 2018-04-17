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

#ifndef PH_FILE_HANDLE_H
#define PH_FILE_HANDLE_H

#include <uv.h>

#include "src/ph_string.h"

typedef struct _ph_file_handle_t {
    uv_fs_t fs;
    // uv_fs_event_t event;
    uv_pipe_t file_pipe;
    uv_write_t write;
    char *name;
    char *buffer;
    int fd;
    zend_long buffer_size;
    zend_long file_size;
    // zend_function *read_function;
    // zend_function *monitor_function;
    ph_string_t actor_ref;
    int actor_restart_count; // maintains actor version number
    zend_object obj;
} ph_file_handle_t;

void ph_file_handle_ce_init(void);

#endif
