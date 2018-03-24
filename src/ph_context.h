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
    void *r8; // 24
    void *r9; // 32
    void *r10; // 40
    void *r11; // 48
    void *r12; // 56
    void *r13; // 64
    void *r14; // 72
    void *r15; // 80
    void *stack_space; // 88
    void (*cb)(void); // 96
    int stack_size; // 104
#ifdef PH_FIXED_STACK_SIZE
    int started; // 108
    void *aligned_stack_space; // 112
# ifdef ZEND_DEBUG
    unsigned int register_valgrind_stack;
# endif
#endif
} ph_mcontext_t;

typedef struct _ph_vmcontext_t {
    zend_vm_stack vm_stack;
    zval *vm_stack_top;
    zval *vm_stack_end;
} ph_vmcontext_t;

typedef struct _ph_context_t {
    ph_mcontext_t mc;
    ph_vmcontext_t vmc;
} ph_context_t;

#define PH_VM_STACK_SIZE 256 // starting size of PHP's VM stack

#ifdef PH_FIXED_STACK_SIZE
# define STACK_ALIGNMENT 16 // Ensure 16 byte stack alignment (for OS X)

extern void ph_mcontext_get(ph_mcontext_t *mc);
extern void ph_mcontext_set(ph_mcontext_t *mc);
extern void ph_mcontext_swap(ph_mcontext_t *from_mc, ph_mcontext_t *to_mc);
#else
# ifdef PH_UNFIXED_STACK_SIZE_SWAP
extern void ph_mcontext_swap(ph_mcontext_t *from_mc, ph_mcontext_t *to_mc, int action);
# else
extern void ph_mcontext_start(ph_mcontext_t *mc, void (*cb)(void));
extern void ph_mcontext_resume(ph_mcontext_t *from_mc, ph_mcontext_t *to_mc);
extern void ph_mcontext_interrupt(ph_mcontext_t *from_mc, ph_mcontext_t *to_mc);
# endif
#endif
void ph_mcontext_init(ph_mcontext_t *mc, void (*cb)(void));
void ph_mcontext_reset(ph_mcontext_t *mc);
void ph_mcontext_free(ph_mcontext_t *mc);

void ph_vmcontext_get(ph_vmcontext_t *vmc);
void ph_vmcontext_set(ph_vmcontext_t *vmc);
void ph_vmcontext_swap(ph_vmcontext_t *from_vmc, ph_vmcontext_t *to_vmc);

#endif
