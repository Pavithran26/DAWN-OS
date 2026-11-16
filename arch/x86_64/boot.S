/* SPDX-License-Identifier: Unlicense */
.section .text.boot, "ax"
.code64
.global _start
.type _start, @function
.extern kernel_main
.extern dawn_boot_stack_top

_start:
    cli
    cld
    leaq dawn_boot_stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp
    call kernel_main

.Lhalt:
    cli
    hlt
    jmp .Lhalt
.size _start, . - _start

.section .bss, "aw", @nobits
.align 16
.global dawn_boot_stack
.type dawn_boot_stack, @object
dawn_boot_stack:
    .skip 65536
.global dawn_boot_stack_top
.type dawn_boot_stack_top, @object
dawn_boot_stack_top:
.size dawn_boot_stack, dawn_boot_stack_top - dawn_boot_stack
