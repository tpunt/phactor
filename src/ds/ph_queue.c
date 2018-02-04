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

#include "php_phactor.h"
#include "src/classes/actor_system.h"

void ph_queue_init(ph_queue_t *queue, void (*dtor)(void *))
{
    queue->elements = NULL;
    queue->last = NULL;
    queue->size = 0;
    queue->dtor = dtor;
    pthread_mutex_init(&queue->lock, NULL);
}

void ph_queue_push(ph_queue_t *queue, void *element)
{
    linked_list_t *ll = malloc(sizeof(linked_list_t));

    ll->element = element;
    ll->next = NULL;

    if (queue->elements) {
        queue->last->next = ll;
    } else {
        queue->elements = ll;
    }

    queue->last = ll;
    ++queue->size;
}

void *ph_queue_pop(ph_queue_t *queue)
{
    void *element = NULL;

    if (queue->elements) {
        linked_list_t *ll = queue->elements;

        queue->elements = queue->elements->next;

        if (!queue->elements) {
            queue->last = NULL;
        }

        element = ll->element;
        free(ll);
        --queue->size;
    }

    return element;
}

int ph_queue_size(ph_queue_t *queue)
{
    return queue->size;
}

void ph_queue_destroy(ph_queue_t *queue)
{
    while (queue->size) {
        queue->dtor(ph_queue_pop(queue));
    }

    pthread_mutex_destroy(&queue->lock);
}
