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

#ifndef PH_CONTEXT_H
#define PH_CONTEXT_H

#include <main/php.h>

typedef struct _ph_mcontext_t {
    void *rbx; // 0
    void *rbp; // 8
    void *rsp; // 16
    void *r8;  // 24
    void *r9;  // 32
    void *r10; // 40
    void *r11; // 48
    void *r12; // 56
    void *r13; // 64
    void *r14; // 72
    void *r15; // 80
} ph_mcontext_t;

typedef struct _ph_context_t {
    void *stack_space;
    int stack_size;
    int started;
    void (*cb)(void);
    ph_mcontext_t mc;
    void *aligned_stack_space;
} ph_context_t;

#define STACK_SIZE 1 << 15
#define STACK_ALIGNMENT 16 // Ensure 16 byte stack alignment (for OS X)

extern void ph_context_get(ph_context_t *c);
extern void ph_context_swap(ph_context_t *from, ph_context_t *to);
extern void ph_context_set(ph_context_t *c);
void ph_context_init(ph_context_t *c, void (*cb)(void));
void ph_context_reset(ph_context_t *c);

void ph_vm_context_get(zend_executor_globals *eg);
void ph_vm_context_set(zend_executor_globals *eg);

#endif
