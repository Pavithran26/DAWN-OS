/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_GDT_H
#define DAWN_GDT_H
#include <stdbool.h>
#include <stdint.h>
#define DAWN_KERNEL_CODE_SELECTOR 0x08U
#define DAWN_KERNEL_DATA_SELECTOR 0x10U
#define DAWN_USER_DATA_SELECTOR 0x18U
#define DAWN_USER_CODE_SELECTOR 0x20U
#define DAWN_TSS_SELECTOR 0x28U
#define DAWN_PROCESS_KERNEL_STACK_SIZE (16U * 1024U)
bool gdt_tss_initialize(void);
bool gdt_tss_set_kernel_stack(uint64_t stack_top);
uint64_t gdt_tss_kernel_stack_top(void);
/* Fixed-size per-process privilege stacks; serialize with interrupts disabled. */
bool gdt_tss_allocate_kernel_stack(uint64_t *stack_top);
bool gdt_tss_activate_kernel_stack(uint64_t stack_top);
bool gdt_tss_release_kernel_stack(uint64_t stack_top);
#ifdef DAWN_KERNEL_TEST
bool gdt_tss_self_test(void);
#endif
#endif
