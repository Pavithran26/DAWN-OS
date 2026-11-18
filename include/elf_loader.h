/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_ELF_LOADER_H
#define DAWN_ELF_LOADER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_ELF64_MAX_PROGRAM_HEADERS 32U
#define DAWN_ELF64_MAX_IMAGE_PAGES 256U
#define DAWN_ELF64_STACK_PAGES 4U
#define DAWN_ELF64_STACK_TOP 0x00007ffffff00000ULL

struct elf64_image_info {
    uint64_t entry;
    uint16_t load_segment_count;
};

struct elf64_loaded_process {
    uint64_t entry;
    uint64_t user_stack_top;
    uint16_t load_segment_count;
    uint16_t mapped_page_count;
};

/* Validate the supported static x86-64 ELF64 executable subset. */
bool elf64_validate_image(const void *image,size_t image_size,
                          struct elf64_image_info *info);
/* Loads PT_LOAD pages, copies file data, zeroes BSS, allocates a user stack,
 * and transitions the supplied CREATED process to READY. */
bool elf64_load_process(uint64_t process_id,const void *image,size_t image_size,
                        struct elf64_loaded_process *loaded);

#ifdef DAWN_KERNEL_TEST
bool elf64_loader_self_test(void);
bool elf64_build_test_exit_image(void *buffer,size_t capacity,size_t *image_size);
#endif
#endif
