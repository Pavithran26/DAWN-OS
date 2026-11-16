/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PHYSICAL_MEMORY_H
#define DAWN_PHYSICAL_MEMORY_H
#include <stdbool.h>
#include <stdint.h>
#include "limine_requests.h"
#define PHYSICAL_ALLOCATOR_MAX_ADDRESS (4ULL * 1024ULL * 1024ULL * 1024ULL)
#define PHYSICAL_ALLOCATOR_PAGE_SIZE 4096ULL
#define PHYSICAL_ALLOCATOR_MAX_FRAMES (PHYSICAL_ALLOCATOR_MAX_ADDRESS / PHYSICAL_ALLOCATOR_PAGE_SIZE)
struct physical_allocator_stats { uint64_t managed_frames; uint64_t free_frames; uint64_t tracked_frames; };
bool physical_allocator_initialize(const struct limine_memmap_response *map);
bool physical_allocator_allocate(uint64_t *physical_address);
bool physical_allocator_free(uint64_t physical_address);
void physical_allocator_get_stats(struct physical_allocator_stats *stats);
#ifdef DAWN_KERNEL_TEST
bool physical_allocator_self_test(void);
#endif
#endif
