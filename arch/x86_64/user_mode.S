/* SPDX-License-Identifier: Unlicense */
.section .text, "ax"
.code64
.global arch_enter_user_mode
.type arch_enter_user_mode, @function
arch_enter_user_mode:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    pushq $0x1b
    pushq %rsi
    pushq $0x2
    pushq $0x23
    pushq %rdi
    iretq
.size arch_enter_user_mode, . - arch_enter_user_mode
.section .note.GNU-stack, "", @progbits
