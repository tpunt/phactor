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

#include "src/ph_message.h"

ph_message_t *ph_msg_create(ph_string_t *from_actor_ref, ph_entry_t *message)
{
    ph_message_t *new_message = malloc(sizeof(ph_message_t));

    new_message->from_actor_ref = *from_actor_ref;
    new_message->message = message;

    return new_message;
}

void ph_msg_free(void *message_void)
{
    ph_message_t *message = message_void;
    ph_str_value_free(&message->from_actor_ref);
    ph_entry_free(message->message);
    free(message);
}
