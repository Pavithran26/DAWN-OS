/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PAGE_TABLE_H
#define DAWN_PAGE_TABLE_H
#include <stdbool.h>
#include <stdint.h>

enum vm_page_flags {
    VM_PAGE_WRITE = 1U << 0,
    VM_PAGE_USER = 1U << 1,
    VM_PAGE_EXECUTE = 1U << 2,
    VM_PAGE_NO_EXECUTE = 1U << 3
};

/* vm_map_page accepts WRITE, USER and EXECUTE.
 * vm_translate reports NO_EXECUTE for an effective NX mapping. */
bool vm_manager_initialize(uint64_t hhdm_offset);
bool vm_map_page(uint64_t virtual_address, uint64_t physical_address,
                 uint32_t flags);
bool vm_unmap_page(uint64_t virtual_address);
bool vm_translate(uint64_t virtual_address, uint64_t *physical_address,
                  uint32_t *flags);
#ifdef DAWN_KERNEL_TEST
bool vm_manager_self_test(void);
#endif
#endif
