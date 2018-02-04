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

#ifndef PH_QUEUE_H
#define PH_QUEUE_H

#include <pthread.h>

typedef struct _linked_list_t {
    void *element;
    struct _linked_list_t *next;
} linked_list_t;

typedef struct _ph_queue_t {
    linked_list_t *elements;
    linked_list_t *last;
    int size;
    void (*dtor)(void *);
    pthread_mutex_t lock;
} ph_queue_t;

void ph_queue_init(ph_queue_t *queue, void (*dtor)(void *));
void ph_queue_push(ph_queue_t *queue, void *element);
void *ph_queue_pop(ph_queue_t *queue);
int ph_queue_size(ph_queue_t *queue);
void ph_queue_destroy(ph_queue_t *queue);

#endif
