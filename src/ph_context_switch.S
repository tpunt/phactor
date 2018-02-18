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

FN(ph_mcontext_swap)
    pushq %rbp
    movq %rsp, %rbp

## action (%rdx):
##   0 = nothing to do (we are executing an actor from the start, which requires a fresh context)
##   1 = save the context (we hit an interruption, so save part of the main stack to the actor context and reset the main stack)
##   2 = restore the context (we want to resume the context, so reload the partially saved main stack from the actor context)
##
## if (action == 1) { // save context
##     to_mc->stack_size = %rsp - to_mc->rsp
##     from_mc->stack_space = realloc(from_mc->stack_space, from_mc->stack_size)
##     memcpy(from_mc->stack_space, from_mc->rsp, from_mc->stack_size)
## }
## // save general purpose registers

    cmpb $1, %dl
    jne L3
    ## action == 1 (save part of the C stack)
    movq 16(%rsi), %rax      ## calculate stack size (old_rsp - current_rsp)
    subq %rsp, %rax
    movq %rax, 104(%rdi)

    pushq %rdx
    pushq %rdi
    pushq %rsi
    pushq $0                 ## keeps stack aligned to 16 bytes

    movq 104(%rdi), %rsi
    movq 88(%rdi), %rdi
    callq _realloc
    movq 8(%rsp), %rsi
    movq 16(%rsp), %rdi
    movq %rax, 88(%rdi)

    movq %rsp, %rsi
    addq $32, %rsi           ## skip past the 4 pushes we made above
    movq 104(%rdi), %rdx
    movq 88(%rdi), %rdi
    callq _memcpy

    popq %rax
    popq %rsi
    popq %rdi
    popq %rdx
L3:

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

## if (action) {
##     if (action == 2) { // restore the actor stack
##         memcpy(%rsp, to_mc->rsp, to_mc->stack_size)
##    }
##    // restore general purpose registers
##    // jump past calling code
## }
##

    cmpb $0, %dl
    je L4
    ## action == (1 | 2)
    cmpb $2, %dl
    jne L5
    ## action == 2
    subq 104(%rsi), %rsp
    movq %rsp, %rdi

    pushq %rsi
    pushq $0                 ## ensure 16 byte stack alignment

    movq 104(%rsi), %rdx
    movq 88(%rsi), %rsi
    callq _memcpy

    popq %rax
    popq %rsi

L5:
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

    jmp L2

L4:
    callq *96(%rsi)

L2:
    popq %rbp
    ret
    .cfi_endproc

#endif
