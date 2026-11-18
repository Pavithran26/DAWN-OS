/* SPDX-License-Identifier: 0BSD
 * Minimal Limine protocol declarations derived from limine-protocol/include/limine.h.
 * Copyright (C) 2022-2026 Mintsuki and contributors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef DAWN_LIMINE_REQUESTS_H
#define DAWN_LIMINE_REQUESTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIMINE_REQUESTS_START_MARKER \
    0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL, \
    0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL
#define LIMINE_REQUESTS_END_MARKER \
    0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL
#define LIMINE_BASE_REVISION(N) \
    0xf9562b2d5c95a6c8ULL, 0x6a7b384944536bdcULL, (N)
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)
#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL

#define LIMINE_FRAMEBUFFER_REQUEST_ID \
    LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL
#define LIMINE_MEMMAP_REQUEST_ID \
    LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL
#define LIMINE_HHDM_REQUEST_ID \
    LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL
#define LIMINE_PAGING_MODE_REQUEST_ID \
    LIMINE_COMMON_MAGIC, 0x95c1a0edab0944cbULL, 0xa4e5cb3842f7488aULL
#define LIMINE_PAGING_MODE_X86_64_4LVL 0ULL
#define LIMINE_PAGING_MODE_X86_64_5LVL 1ULL
#define LIMINE_RSDP_REQUEST_ID \
    LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43ULL, 0x27637845accdcf3cULL

#define LIMINE_MEMMAP_USABLE 0ULL
#define LIMINE_MEMMAP_RESERVED 1ULL
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2ULL
#define LIMINE_MEMMAP_ACPI_NVS 3ULL
#define LIMINE_MEMMAP_BAD_MEMORY 4ULL
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5ULL
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6ULL
#define LIMINE_MEMMAP_FRAMEBUFFER 7ULL
#define LIMINE_MEMMAP_RESERVED_MAPPED 8ULL

struct limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    struct limine_video_mode **modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

extern volatile struct limine_framebuffer_request limine_framebuffer_request;

static inline bool framebuffer_response_valid(void) {
    struct limine_framebuffer_response *response =
        limine_framebuffer_request.response;

    return response != NULL && response->framebuffer_count > 0ULL &&
           response->framebuffers != NULL && response->framebuffers[0] != NULL;
}

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

struct limine_paging_mode_response {
    uint64_t revision;
    uint64_t mode;
};

struct limine_paging_mode_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_paging_mode_response *response;
    uint64_t mode;
    uint64_t max_mode;
    uint64_t min_mode;
};

struct limine_rsdp_response {
    uint64_t revision;
    void *address;
};

struct limine_rsdp_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_rsdp_response *response;
};

_Static_assert(sizeof(struct limine_memmap_entry) == 24U,
               "Limine memory map entry ABI size mismatch");
_Static_assert(sizeof(struct limine_memmap_response) == 24U,
               "Limine memory map response ABI size mismatch");
_Static_assert(sizeof(struct limine_memmap_request) == 48U,
               "Limine memory map request ABI size mismatch");
_Static_assert(sizeof(struct limine_hhdm_response) == 16U,
               "Limine HHDM response ABI size mismatch");
_Static_assert(sizeof(struct limine_hhdm_request) == 48U,
               "Limine HHDM request ABI size mismatch");
_Static_assert(sizeof(struct limine_paging_mode_response) == 16U,
               "Limine paging-mode response ABI size mismatch");
_Static_assert(sizeof(struct limine_paging_mode_request) == 72U,
               "Limine paging-mode request ABI size mismatch");
_Static_assert(sizeof(struct limine_rsdp_response) == 16U,
               "Limine RSDP response ABI size mismatch");
_Static_assert(sizeof(struct limine_rsdp_request) == 48U,
               "Limine RSDP request ABI size mismatch");
_Static_assert(sizeof(struct limine_framebuffer_response) == 24U,
               "Limine framebuffer response ABI size mismatch");
_Static_assert(sizeof(struct limine_framebuffer_request) == 48U,
               "Limine framebuffer request ABI size mismatch");

#endif
