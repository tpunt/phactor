#ifdef __APPLE__
# define FN(name) \
    .globl _##name; \
    .align 4, 0x90; \
    _##name##: \
    .cfi_startproc

# define CALL(fn) _##fn
#else
# define FN(name) \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc

# define CALL(fn) fn##@PLT
#endif

#ifdef PH_FIXED_STACK_SIZE
FN(ph_mcontext_get)
    pushq %rbp
    movq %rsp, %rbp

    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rsp, 16(%rdi)
    movq %r8, 24(%rdi)
    movq %r9, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r11, 48(%rdi)
    movq %r12, 56(%rdi)
    movq %r13, 64(%rdi)
    movq %r14, 72(%rdi)
    movq %r15, 80(%rdi)

    popq %rbp
    ret
    .cfi_endproc

FN(ph_mcontext_set)
    pushq %rbp
    movq %rsp, %rbp

    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %rsp
    movq 24(%rdi), %r8
    movq 32(%rdi), %r9
    movq 40(%rdi), %r10
    movq 48(%rdi), %r11
    movq 56(%rdi), %r12
    movq 64(%rdi), %r13
    movq 72(%rdi), %r14
    movq 80(%rdi), %r15

    popq %rbp
    ret
    .cfi_endproc

FN(ph_mcontext_swap)
    pushq %rbp
    movq %rsp, %rbp

    ## save the context (into %rdi)
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rsp, 16(%rdi)
    movq %r8, 24(%rdi)
    movq %r9, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r11, 48(%rdi)
    movq %r12, 56(%rdi)
    movq %r13, 64(%rdi)
    movq %r14, 72(%rdi)
    movq %r15, 80(%rdi)

    ## set the new context (from %rsi)
    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %rsp
    movq 24(%rsi), %r8
    movq 32(%rsi), %r9
    movq 40(%rsi), %r10
    movq 48(%rsi), %r11
    movq 56(%rsi), %r12
    movq 64(%rsi), %r13
    movq 72(%rsi), %r14
    movq 80(%rsi), %r15

    cmpb $0, 108(%rsi) # to_mc->started
    jne L2
    cmpq $0, 96(%rsi)
    je L2
    # save some registers?
    movb $1, 108(%rsi)
    callq *96(%rsi)
    # use %rax ?
L2:

    popq %rbp
    ret
    .cfi_endproc

#else

FN(ph_mcontext_start)
    pushq %rbp
    movq %rsp, %rbp

    ## save the current context
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rsp, 16(%rdi)
    movq %r8, 24(%rdi)
    movq %r9, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r11, 48(%rdi)
    movq %r12, 56(%rdi)
    movq %r13, 64(%rdi)
    movq %r14, 72(%rdi)
    movq %r15, 80(%rdi)

    callq *%rsi

    popq %rbp
    ret
    .cfi_endproc

FN(ph_mcontext_resume)
    pushq %rbp
    movq %rsp, %rbp

    ## save the current context
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rsp, 16(%rdi)
    movq %r8, 24(%rdi)
    movq %r9, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r11, 48(%rdi)
    movq %r12, 56(%rdi)
    movq %r13, 64(%rdi)
    movq %r14, 72(%rdi)
    movq %r15, 80(%rdi)

    ## restore the old context
    xorq %rdi, %rdi
    movl 108(%rsi), %edi
    subq %rdi, %rsp

    movq %rsp, %rdi
    movq %rsi, %rbx
    xorq %rdx, %rdx
    movl 108(%rsi), %edx
    movq 88(%rsi), %rsi
    callq CALL(memcpy)
    movq %rbx, %rsi

    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %rsp
    movq 24(%rsi), %r8
    movq 32(%rsi), %r9
    movq 40(%rsi), %r10
    movq 48(%rsi), %r11
    movq 56(%rsi), %r12
    movq 64(%rsi), %r13
    movq 72(%rsi), %r14
    movq 80(%rsi), %r15

    popq %rbp
    ret
    .cfi_endproc

FN(ph_mcontext_interrupt)
    pushq %rbp
    movq %rsp, %rbp

    ## used_stack_size = old_rsp - current_rsp
    movq 16(%rsi), %rax
    subq %rsp, %rax
    movl %eax, 108(%rdi)

    pushq %rdi
    pushq %rsi

    ## if (!(allocated_stack_size <= used_stack_size))
    cmpl 104(%rdi), %eax
    jle L6

    xorq %rsi, %rsi
    movl 108(%rdi), %esi
    movl %esi, 104(%rdi)     ## allocated_stack_size = used_stack_size
    movq 88(%rdi), %rdi
    callq CALL(realloc)
    movq 0(%rsp), %rsi
    movq 8(%rsp), %rdi
    movq %rax, 88(%rdi)

L6:
    movq %rsp, %rsi
    addq $16, %rsi           ## skip past 2 pushes onto stack
    xorq %rdx, %rdx
    movl 108(%rdi), %edx
    movq 88(%rdi), %rdi
    callq CALL(memcpy)

    popq %rsi
    popq %rdi

    ## save the current context
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rsp, 16(%rdi)
    movq %r8, 24(%rdi)
    movq %r9, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r11, 48(%rdi)
    movq %r12, 56(%rdi)
    movq %r13, 64(%rdi)
    movq %r14, 72(%rdi)
    movq %r15, 80(%rdi)

    ## restore the old context
    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %rsp
    movq 24(%rsi), %r8
    movq 32(%rsi), %r9
    movq 40(%rsi), %r10
    movq 48(%rsi), %r11
    movq 56(%rsi), %r12
    movq 64(%rsi), %r13
    movq 72(%rsi), %r14
    movq 80(%rsi), %r15

    popq %rbp
    ret
    .cfi_endproc

#endif
