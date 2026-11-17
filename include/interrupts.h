/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_INTERRUPTS_H
#define DAWN_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    /* Present only after a CPL3->CPL0 transition through TSS.RSP0. */
    uint64_t user_rsp;
    uint64_t user_ss;
};

struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame);

/* Installs exception/IRQ gates and remaps/masks the legacy 8259 PIC. IF stays
 * clear. The PIT IRQ is enabled only inside the bounded self-test. */
bool interrupts_initialize(void);

#ifdef DAWN_KERNEL_TEST
bool interrupts_self_test(void);
#endif

#endif
