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

#ifndef PH_VECTOR_H
#define PH_VECTOR_H

#include <pthread.h>

typedef struct _ph_vector_t {
    void **values;
    int size;
    int used;
    void (*dtor)(void *);
    pthread_mutex_t lock;
} ph_vector_t;

void ph_vector_init(ph_vector_t *vector, int size, void (*dtor)(void *));
void ph_vector_push(ph_vector_t *vector, void *value);
void *ph_vector_pop(ph_vector_t *vector);
int ph_vector_size(ph_vector_t *vector);
void ph_vector_destroy(ph_vector_t *vector);

#endif
