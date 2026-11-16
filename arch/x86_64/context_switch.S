/* SPDX-License-Identifier: Unlicense */
.section .text, "ax"
.code64
.global arch_context_switch
.type arch_context_switch, @function
arch_context_switch:
    /* SysV: RDI=&old_rsp, RSI=&new_rsp. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size arch_context_switch, . - arch_context_switch

.global kernel_thread_bootstrap_trampoline
.type kernel_thread_bootstrap_trampoline, @function
kernel_thread_bootstrap_trampoline:
    /* New context has a 16-byte aligned RSP at trampoline entry. */
    call kernel_thread_bootstrap
    ud2
.size kernel_thread_bootstrap_trampoline, . - kernel_thread_bootstrap_trampoline
.section .note.GNU-stack, "", @progbits
