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

#include <stdlib.h>

#include "src/ds/ph_vector.h"

static void ph_vector_space_check(ph_vector_t *vector)
{
    if (vector->used == vector->size) {
        vector->size = vector->size ? vector->size << 1 : 1;
        vector->values = realloc(vector->values, vector->size * sizeof(void *)); // @todo success check
    }
}

void ph_vector_init(ph_vector_t *vector, int size, void (*dtor)(void *))
{
    vector->values = calloc(size, sizeof(void *));
    vector->size = size;
    vector->used = 0;
    vector->dtor = dtor;
    pthread_mutex_init(&vector->lock, NULL);
}

void ph_vector_push(ph_vector_t *vector, void *value)
{
    ph_vector_space_check(vector);
    vector->values[vector->used++] = value;
}

void *ph_vector_pop(ph_vector_t *vector)
{
    if (!vector->used) {
        return NULL;
    }

    // @todo resize if used = 1/4 of size?

    return vector->values[--vector->used];
}

int ph_vector_size(ph_vector_t *vector)
{
    return vector->used;
}

void ph_vector_destroy(ph_vector_t *vector)
{
    for (int i = 0; i < vector->used; ++i) {
        vector->dtor(vector->values[i]);
    }

    free(vector->values);
}
