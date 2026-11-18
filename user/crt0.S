/* SPDX-License-Identifier: Unlicense */
.global _start
.extern main
.extern dawn_exit

.section .text
_start:
    /* Clear frame pointer */
    xorq %rbp, %rbp

    /* If argc was passed on stack (SysV ABI convention) */
    popq %rdi

    /* argv pointer is now at %rsp */
    movq %rsp, %rsi

    /* Compute envp = argv + argc + 1 */
    leaq 8(%rsp, %rdi, 8), %rdx

    /* Align stack to 16 bytes */
    movq %rsp, %rax
    andq $-16, %rsp
    pushq %rax

    /* Call main(argc, argv, envp) */
    call main

    /* Exit with return value */
    movq %rax, %rdi
    call dawn_exit

1:
    hlt
    jmp 1b
