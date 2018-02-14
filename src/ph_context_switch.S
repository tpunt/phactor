#ifdef __APPLE__
# define FN(name) \
    .globl _##name; \
    .align 4, 0x90; \
    _##name##: \
    .cfi_startproc
#else
# define FN(name) \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc
#endif

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

    cmpb $0, 100(%rsi) # just use %rip ?
    jne L2
    cmpq $0, 104(%rsi)
    je L2
    # save some registers?
    movb $1, 100(%rsi)
    callq *104(%rsi)
    # use %rax ?

L2:
    popq %rbp
    ret
    .cfi_endproc
