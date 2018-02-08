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

FN(ph_context_get)
    pushq %rbp
    movq %rsp, %rbp

    leaq 24(%rdi), %rdi
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

FN(ph_context_set)
    pushq %rbp
    movq %rsp, %rbp

    leaq 24(%rdi), %rdi
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

FN(ph_context_swap)
    pushq %rbp
    movq %rsp, %rbp

    ## save the context (into %rdi)
    leaq 24(%rdi), %rdi
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
    leaq 24(%rsi), %rdi
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

    cmpb $0, 12(%rsi) # just use %rip ?
    jne L2
    cmpq $0, 16(%rsi)
    je L2
    # save some registers?
    movb $1, 12(%rsi)
    callq *16(%rsi)
    # use %rax ?

L2:
    popq %rbp
    ret
    .cfi_endproc
