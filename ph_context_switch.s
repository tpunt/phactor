.section	__TEXT,__text,regular,pure_instructions
.macosx_version_min 10, 10

  .globl _ph_get_context
  .align 4, 0x90
_ph_get_context:                               ## @ph_get_context
  .cfi_startproc
  pushq %rbp
  movq %rsp, %rbp

  leaq 24(%rdi), %rdi
  movq %rbx, 8(%rdi)
  movq %rbp, 48(%rdi)
  movq %rsp, 56(%rdi)
  movq %r8, 64(%rdi)
  movq %r9, 72(%rdi)
  movq %r10, 80(%rdi)
  movq %r11, 88(%rdi)
  movq %r12, 96(%rdi)
  movq %r13, 104(%rdi)
  movq %r14, 112(%rdi)
  movq %r15, 120(%rdi)

  popq %rbp
  ret
  .cfi_endproc

  .globl _ph_set_context
  .align 4, 0x90
_ph_set_context:                               ## @ph_set_context
  .cfi_startproc
  pushq %rbp
  movq %rsp, %rbp

  leaq 24(%rdi), %rdi
  movq 8(%rdi), %rbx
  movq 48(%rdi), %rbp
  movq 56(%rdi), %rsp
  movq 64(%rdi), %r8
  movq 72(%rdi), %r9
  movq 80(%rdi), %r10
  movq 88(%rdi), %r11
  movq 96(%rdi), %r12
  movq 104(%rdi), %r13
  movq 112(%rdi), %r14
  movq 120(%rdi), %r15

  popq %rbp
  ret
  .cfi_endproc

  .globl _ph_swap_context
  .align 4, 0x90
_ph_swap_context:                               ## @ph_swap_context
  .cfi_startproc
  pushq %rbp
  movq %rsp, %rbp

  ## save the context (into %rdi)
  leaq 24(%rdi), %rdi
  movq %rbx, 8(%rdi)
  movq %rbp, 48(%rdi)
  movq %rsp, 56(%rdi)
  movq %r8, 64(%rdi)
  movq %r9, 72(%rdi)
  movq %r10, 80(%rdi)
  movq %r11, 88(%rdi)
  movq %r12, 96(%rdi)
  movq %r13, 104(%rdi)
  movq %r14, 112(%rdi)
  movq %r15, 120(%rdi)

  ## set the new context (from %rsi)
  leaq 24(%rsi), %rdi
  movq 8(%rdi), %rbx
  movq 48(%rdi), %rbp
  movq 56(%rdi), %rsp
  movq 64(%rdi), %r8
  movq 72(%rdi), %r9
  movq 80(%rdi), %r10
  movq 88(%rdi), %r11
  movq 96(%rdi), %r12
  movq 104(%rdi), %r13
  movq 112(%rdi), %r14
  movq 120(%rdi), %r15

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
