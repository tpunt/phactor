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

#ifndef PH_CONTEXT_H
#define PH_CONTEXT_H

typedef struct _ph_mcontext_t {
    void *rax; // 0 (unused)
    void *rbx; // 8
    void *rcx; // 16 (unused)
    void *rdx; // 24 (unused)
    void *rdi; // 32 (unused)
    void *rsi; // 40 (unused)
    void *rbp; // 48
    void *rsp; // 56
    void *r8;  // 64
    void *r9;  // 72
    void *r10; // 80
    void *r11; // 88
    void *r12; // 96
    void *r13; // 104
    void *r14; // 112
    void *r15; // 120
    void *rip; // 128 (unused)
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

extern void ph_get_context(ph_context_t *c);
extern void ph_swap_context(ph_context_t *from, ph_context_t *to);
extern void ph_set_context(ph_context_t *c);
void ph_init_context(ph_context_t *c, void (*cb)(void));
void ph_reset_context(ph_context_t *c);

#endif
