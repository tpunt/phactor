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

#include "ph_context.h"
#include <stdlib.h>
#include <string.h>

#ifdef ZEND_DEBUG
# include "valgrind/valgrind.h"
#endif

void ph_mcontext_init(ph_mcontext_t *mc, void (*cb)(void))
{
#ifdef PH_FIXED_STACK_SIZE
    mc->allocated_stack_size = PH_FIXED_STACK_SIZE;
    mc->stack_space = calloc(1, mc->allocated_stack_size + STACK_ALIGNMENT - 1);
    mc->aligned_stack_space = (void *)((uintptr_t)mc->stack_space + (STACK_ALIGNMENT - 1) & ~(STACK_ALIGNMENT - 1));
# ifdef ZEND_DEBUG
    mc->register_valgrind_stack = VALGRIND_STACK_REGISTER(mc->aligned_stack_space, mc->aligned_stack_space + mc->allocated_stack_size);
# endif
#else
    mc->allocated_stack_size = 0;
    mc->used_stack_size = 0;
    mc->stack_space = NULL;
#endif
    mc->cb = cb;

    ph_mcontext_reset(mc);
}

void ph_mcontext_reset(ph_mcontext_t *mc)
{
    memset(mc, 0, sizeof(void *) * 11);

#ifdef PH_FIXED_STACK_SIZE
    mc->started = 0;

    // assumes the stack always grows downwards
    mc->rbp = mc->aligned_stack_space + mc->allocated_stack_size;
    mc->rsp = mc->aligned_stack_space + mc->allocated_stack_size;
#endif
}

void ph_mcontext_free(ph_mcontext_t *mc)
{
#ifdef PH_FIXED_STACK_SIZE
    free(mc->stack_space);
# ifdef ZEND_DEBUG
    VALGRIND_STACK_DEREGISTER(mc->register_valgrind_stack);
# endif
#else
    if (mc->stack_space) {
        free(mc->stack_space);
    }
#endif
}

void ph_vmcontext_get(ph_vmcontext_t *vmc)
{
    // @todo for now, we only save vm stack stuff. In future, more things will
    // need to be saved for this to work properly
    vmc->vm_stack_top = EG(vm_stack_top);
    vmc->vm_stack_end = EG(vm_stack_end);
    vmc->vm_stack = EG(vm_stack);
}

void ph_vmcontext_set(ph_vmcontext_t *vmc)
{
    // @todo for now, we only restore vm stack stuff. In future, more things
    // will need to be restored for this to work properly
    EG(vm_stack_top) = vmc->vm_stack_top;
    EG(vm_stack_end) = vmc->vm_stack_end;
    EG(vm_stack) = vmc->vm_stack;
}

void ph_vmcontext_swap(ph_vmcontext_t *from_vmc, ph_vmcontext_t *to_vmc)
{
    ph_vmcontext_get(from_vmc);
    ph_vmcontext_set(to_vmc);
}

void ph_vmcontext_clear(zend_vm_stack vm_stack)
{
    // free all pages, except for the last one
    while (vm_stack->prev) {
        zend_vm_stack prev = vm_stack->prev;

        efree(vm_stack);
        vm_stack = prev;
    }
}

void ph_vmcontext_reset(ph_vmcontext_t *vmc)
{
    zend_vm_stack vm_stack = vmc->vm_stack;

    while (vm_stack->prev) {
        vm_stack = vm_stack->prev;
    }

    vmc->vm_stack = vm_stack;
    vmc->vm_stack_top = ZEND_VM_STACK_ELEMENTS(vm_stack);
    vmc->vm_stack_end = (zval*)((char*)vm_stack + PH_VM_STACK_SIZE);
}
