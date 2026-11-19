/* SPDX-License-Identifier: Unlicense */
.section .text, "ax"
.code64
.global dawn_syscall_entry
.extern syscall_dispatch

.data
.align 8
.global dawn_syscall_scratch_user_rsp
dawn_syscall_scratch_user_rsp:
    .quad 0

.global dawn_syscall_kernel_stack_top
dawn_syscall_kernel_stack_top:
    .quad 0

.text
dawn_syscall_entry:
    # Save user RSP into scratch storage
    movq %rsp, dawn_syscall_scratch_user_rsp(%rip)

    # Switch to kernel privilege stack
    movq dawn_syscall_kernel_stack_top(%rip), %rsp

    # Push user return context: user_rsp, r11 (user rflags), rcx (user rip)
    pushq dawn_syscall_scratch_user_rsp(%rip)
    pushq %r11
    pushq %rcx

    # Preserve callee-saved registers
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # Push 7th argument (arg5, currently in %r9) to stack for C ABI
    pushq %r9

    # Re-order registers for C function syscall_dispatch(num, a0, a1, a2, a3, a4, a5)
    # Incoming from user:
    #   %rax = syscall number
    #   %rdi = arg0
    #   %rsi = arg1
    #   %rdx = arg2
    #   %r10 = arg3
    #   %r8  = arg4
    #   %r9  = arg5
    movq %r8, %r9
    movq %r10, %r8
    movq %rdx, %rcx
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq %rax, %rdi

    # Call unified syscall dispatcher in kernel/syscall.c
    call syscall_dispatch

    # Clean up 7th argument from stack
    addq $8, %rsp

    # Restore callee-saved registers
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    # Restore user return context
    popq %rcx
    popq %r11
    popq %rsp

    # Fast return to Ring 3 userspace
    sysretq
.size dawn_syscall_entry, . - dawn_syscall_entry
.section .note.GNU-stack, "", @progbits
