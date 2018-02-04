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

#ifndef PH_MESSAGE_H
#define PH_MESSAGE_H

#include "src/ph_entry.h"
#include "src/ph_string.h"

typedef struct _ph_message_t {
    ph_string_t from_actor_ref; // could just be a pointer - what about remote actors?
    ph_entry_t *message; // why the separate allocation?
} ph_message_t;

ph_message_t *ph_msg_create(ph_string_t *from_actor_ref, ph_entry_t *message);
void ph_msg_free(void *message_void);

#endif
