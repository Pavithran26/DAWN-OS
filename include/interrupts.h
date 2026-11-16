/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_INTERRUPTS_H
#define DAWN_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

/* Installs exception/IRQ gates and remaps/masks the legacy 8259 PIC. IF stays
 * clear. The PIT IRQ is enabled only inside the bounded self-test. */
bool interrupts_initialize(void);

#ifdef DAWN_KERNEL_TEST
bool interrupts_self_test(void);
#endif

#endif
