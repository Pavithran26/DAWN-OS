/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "physical_memory.h"
#define BITS 64ULL
#define WORDS ((PHYSICAL_ALLOCATOR_MAX_FRAMES + BITS - 1ULL) / BITS)
#define MAX_ENTRIES 1024ULL

struct allocator {
    uint64_t *managed, *allocated;
    uint64_t frames, total, free_count, hint;
};
static uint64_t managed_map[WORDS];
static uint64_t allocated_map[WORDS];
static struct allocator live;

static bool bit(const uint64_t *map, uint64_t frame) {
    return (map[(size_t)(frame / BITS)] & (1ULL << (frame % BITS))) != 0ULL;
}
static void set(uint64_t *map, uint64_t frame) {
    map[(size_t)(frame / BITS)] |= 1ULL << (frame % BITS);
}
static void clear(uint64_t *map, uint64_t frame) {
    map[(size_t)(frame / BITS)] &= ~(1ULL << (frame % BITS));
}

/* Validate all entries before opening any frame to allocation. */
static bool valid_map(const struct limine_memmap_response *map) {
    uint64_t prev=0ULL, high=0ULL, high_alloc=0ULL;
    bool have=false;
    if (!map || !map->entries || map->entry_count==0ULL ||
        map->entry_count>MAX_ENTRIES) return false;
    for (uint64_t i=0;i<map->entry_count;i++) {
        const struct limine_memmap_entry *e=map->entries[i];
        uint64_t end=0ULL;
        bool alloc;
        if (!e || e->length==0ULL || e->length>UINT64_MAX-e->base) return false;
        end=e->base+e->length;
        alloc=(e->type==LIMINE_MEMMAP_USABLE ||
               e->type==LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
        if (have && e->base<prev) return false;
        if (alloc) {
            if ((e->base%4096ULL)!=0ULL || (e->length%4096ULL)!=0ULL ||
                e->base<high) return false;
        } else if (e->base<high_alloc) return false;
        if (end>high) high=end;
        if (alloc && end>high_alloc) high_alloc=end;
        prev=e->base; have=true;
    }
    return true;
}
static bool init_ctx(struct allocator *a,const struct limine_memmap_response *map,
                     uint64_t frames,uint64_t *managed,uint64_t *allocated,
                     size_t capacity) {
    uint64_t words;
    if (!a || !managed || !allocated || frames<=1ULL ||
        frames>PHYSICAL_ALLOCATOR_MAX_FRAMES || !valid_map(map)) return false;
    words=(frames+BITS-1ULL)/BITS;
    if (words>(uint64_t)capacity) return false;
    for (uint64_t i=0;i<words;i++) { managed[(size_t)i]=0ULL; allocated[(size_t)i]=UINT64_MAX; }
    a->managed=managed; a->allocated=allocated; a->frames=frames;
    a->total=0ULL; a->free_count=0ULL; a->hint=1ULL;
    for (uint64_t i=0;i<map->entry_count;i++) {
        const struct limine_memmap_entry *e=map->entries[i];
        uint64_t first,end;
        /* Deliberately do not reclaim bootloader/ACPI/reserved/unknown memory. */
        if (e->type!=LIMINE_MEMMAP_USABLE) continue;
        first=e->base/4096ULL; end=(e->base+e->length)/4096ULL;
        if (first==0ULL) first=1ULL;
        if (first>=frames) continue;
        if (end>frames) end=frames;
        for (uint64_t f=first;f<end;f++) if (!bit(managed,f)) {
            set(managed,f); clear(allocated,f); ++a->total; ++a->free_count;
        }
    }
    return true;
}
static bool alloc_ctx(struct allocator *a,uint64_t *out) {
    uint64_t f;
    if (!a || !out || !a->managed || !a->allocated ||
        a->frames<=1ULL || a->free_count==0ULL) return false;
    for (f=a->hint;f<a->frames;f++) if (bit(a->managed,f)&&!bit(a->allocated,f)) {
        set(a->allocated,f); --a->free_count; a->hint=f+1ULL;
        if (a->hint>=a->frames) a->hint=1ULL;
        *out=f*4096ULL; return true;
    }
    for (f=1ULL;f<a->hint;f++) if (bit(a->managed,f)&&!bit(a->allocated,f)) {
        set(a->allocated,f); --a->free_count; a->hint=f+1ULL;
        if (a->hint>=a->frames) a->hint=1ULL;
        *out=f*4096ULL; return true;
    }
    return false;
}
static bool free_ctx(struct allocator *a,uint64_t address) {
    uint64_t f=address/4096ULL;
    if (!a || !a->managed || !a->allocated || address%4096ULL ||
        f==0ULL || f>=a->frames || !bit(a->managed,f) ||
        !bit(a->allocated,f) || a->free_count>=a->total) return false;
    clear(a->allocated,f); ++a->free_count;
    if (f<a->hint) a->hint=f;
    return true;
}
bool physical_allocator_initialize(const struct limine_memmap_response *map) {
    return init_ctx(&live,map,PHYSICAL_ALLOCATOR_MAX_FRAMES,
                    managed_map,allocated_map,(size_t)WORDS);
}
bool physical_allocator_allocate(uint64_t *address) { return alloc_ctx(&live,address); }
bool physical_allocator_free(uint64_t address) { return free_ctx(&live,address); }
void physical_allocator_get_stats(struct physical_allocator_stats *s) {
    if (!s) return;
    s->managed_frames=live.total; s->free_frames=live.free_count; s->tracked_frames=live.frames;
}
#ifdef DAWN_KERNEL_TEST
bool physical_allocator_self_test(void) {
    uint64_t m[1]={0ULL}, a_bits[1]={0ULL}, got[4]={0ULL,0ULL,0ULL,0ULL}, out=UINT64_MAX;
    struct allocator a={0};
    struct limine_memmap_entry u={.base=0x1000ULL,.length=0x4000ULL,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry r={.base=0x5000ULL,.length=0x1000ULL,.type=LIMINE_MEMMAP_RESERVED};
    struct limine_memmap_entry b={.base=0x6000ULL,.length=0x1000ULL,.type=LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE};
    struct limine_memmap_entry hi={.base=0x9000ULL,.length=0x2000ULL,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *es[]={&u,&r,&b,&hi};
    struct limine_memmap_response map={.revision=0ULL,.entry_count=4ULL,.entries=es};
    if (!init_ctx(&a,&map,8ULL,m,a_bits,1U)||a.total!=4ULL||a.free_count!=4ULL) return false;
    for (size_t i=0;i<4U;i++) if (!alloc_ctx(&a,&got[i])||got[i]!=(i+1ULL)*4096ULL) return false;
    if (a.free_count!=0ULL||alloc_ctx(&a,&out)) return false;
    if (free_ctx(&a,0ULL)||free_ctx(&a,0x5000ULL)||free_ctx(&a,0x6000ULL)||
        free_ctx(&a,0x1001ULL)||free_ctx(&a,0x8000ULL)) return false;
    if (!free_ctx(&a,got[1])||a.free_count!=1ULL||free_ctx(&a,got[1])||
        !alloc_ctx(&a,&out)||out!=got[1]) return false;

    uint64_t em[1]={0ULL}, ea[1]={0ULL};
    struct allocator edge={0};
    struct limine_memmap_entry eu={.base=0x7000ULL,.length=0x3000ULL,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *ees[]={&eu};
    struct limine_memmap_response edge_map={.revision=0ULL,.entry_count=1ULL,.entries=ees};
    if (!init_ctx(&edge,&edge_map,8ULL,em,ea,1U)||edge.total!=1ULL||
        !alloc_ctx(&edge,&out)||out!=0x7000ULL||alloc_ctx(&edge,&out)) return false;

    uint64_t zm[1]={0ULL}, za[1]={0ULL};
    struct allocator zero={0};
    struct limine_memmap_entry zu={.base=0ULL,.length=0x2000ULL,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *zes[]={&zu};
    struct limine_memmap_response zero_map={.revision=0ULL,.entry_count=1ULL,.entries=zes};
    return init_ctx(&zero,&zero_map,8ULL,zm,za,1U)&&zero.total==1ULL&&
           alloc_ctx(&zero,&out)&&out==4096ULL;
}
#endif
