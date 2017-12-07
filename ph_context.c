/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "ph_context.h"
#include <stdlib.h>
#include <string.h>

void ph_init_context(ph_context_t *c, void (*cb)(void))
{
    c->stack_size = STACK_SIZE;
    c->stack_space = calloc(1, c->stack_size + STACK_ALIGNMENT - 1);
    c->aligned_stack_space = (void *)((uintptr_t)c->stack_space + (STACK_ALIGNMENT - 1) & ~(STACK_ALIGNMENT - 1));
    c->cb = cb;

    ph_reset_context(c);
}

void ph_reset_context(ph_context_t *c)
{
    memset(&c->mc, 0, sizeof(ph_mcontext_t));

    c->started = 0;

    // assumes the stack always grows downwards
    c->mc.rbp = c->aligned_stack_space + c->stack_size;
    c->mc.rsp = c->aligned_stack_space + c->stack_size;
}

// free context
