/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PAGE_TABLE_H
#define DAWN_PAGE_TABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum vm_page_flags {
    VM_PAGE_WRITE = 1U << 0,
    VM_PAGE_USER = 1U << 1,
    VM_PAGE_EXECUTE = 1U << 2,
    VM_PAGE_NO_EXECUTE = 1U << 3
};

struct vm_address_space { uint64_t root_physical; };

/* vm_map_page accepts WRITE, USER and EXECUTE.
 * vm_translate reports NO_EXECUTE for an effective NX mapping. */
bool vm_manager_initialize(uint64_t hhdm_offset);
/* Address spaces share supervisor kernel mappings; user frames stay caller-owned. */
bool vm_address_space_create(struct vm_address_space *space);
bool vm_address_space_map_user(struct vm_address_space *space, uint64_t virtual_address, uint64_t physical_address, uint32_t flags);
bool vm_address_space_translate(const struct vm_address_space *space, uint64_t virtual_address, uint64_t *physical_address, uint32_t *flags);
/* CR3 switches are CPL0-only and require IF=0; kernel mappings are shared. */
bool vm_address_space_activate(const struct vm_address_space *space);
bool vm_address_space_activate_kernel(void);
bool vm_address_space_is_active(const struct vm_address_space *space);
bool vm_address_space_destroy(struct vm_address_space *space);
bool vm_map_page(uint64_t virtual_address, uint64_t physical_address,
                 uint32_t flags);
bool vm_map_mmio_page(uint64_t physical_address, uint64_t *virtual_address);
bool vm_unmap_page(uint64_t virtual_address);
bool vm_translate(uint64_t virtual_address, uint64_t *physical_address,
                  uint32_t *flags);
/* User-memory helpers validate every page before copying through the HHDM. */
bool vm_user_range_valid(uint64_t virtual_address, uint64_t length,
                         bool require_write);
bool vm_copy_from_user(void *destination, uint64_t user_source, size_t length);
bool vm_copy_to_user(uint64_t user_destination, const void *source, size_t length);
#ifdef DAWN_KERNEL_TEST
bool vm_manager_self_test(void);
bool vm_address_space_self_test(void);
#endif
#endif
