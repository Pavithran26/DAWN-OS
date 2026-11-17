/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_ELF_LOADER_H
#define DAWN_ELF_LOADER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_ELF64_MAX_PROGRAM_HEADERS 32U

struct elf64_image_info {
    uint64_t entry;
    uint16_t load_segment_count;
};

/* Validate the supported static x86-64 ELF64 executable subset. */
bool elf64_validate_image(const void *image,size_t image_size,
                          struct elf64_image_info *info);

#ifdef DAWN_KERNEL_TEST
bool elf64_loader_self_test(void);
#endif
#endif
