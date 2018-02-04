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

void ph_context_init(ph_context_t *c, void (*cb)(void))
{
    c->stack_size = STACK_SIZE;
    c->stack_space = calloc(1, c->stack_size + STACK_ALIGNMENT - 1);
    c->aligned_stack_space = (void *)((uintptr_t)c->stack_space + (STACK_ALIGNMENT - 1) & ~(STACK_ALIGNMENT - 1));
    c->cb = cb;

    ph_context_reset(c);
}

void ph_context_reset(ph_context_t *c)
{
    memset(&c->mc, 0, sizeof(ph_mcontext_t));

    c->started = 0;

    // assumes the stack always grows downwards
    c->mc.rbp = c->aligned_stack_space + c->stack_size;
    c->mc.rsp = c->aligned_stack_space + c->stack_size;
}

// @todo implement properly
void ph_executor_globals_save(zend_executor_globals *eg)
{
    // eg->current_execute_data = EG(current_execute_data);
    *eg = *TSRMG_BULK_STATIC(executor_globals_id, zend_executor_globals *);
    // eg->vm_stack_top = EG(vm_stack_top);
    // eg->vm_stack_end = EG(vm_stack_end);
    // eg->vm_stack = EG(vm_stack);
    // eg->fake_scope = EG(fake_scope);
    //
}

// @todo implement properly
void ph_executor_globals_restore(zend_executor_globals *eg)
{
    zend_executor_globals *ceg = TSRMG_BULK_STATIC(executor_globals_id, zend_executor_globals *);

    EG(current_execute_data) = eg->current_execute_data;
    // EG(vm_stack_top) = eg->vm_stack_top;
    // EG(vm_stack_end) = eg->vm_stack_end;
    // EG(vm_stack) = eg->vm_stack;
    // EG(fake_scope) = eg->fake_scope;
    //
}

// free context
