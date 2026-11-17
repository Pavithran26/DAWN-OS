/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "page_table.h"
#include "physical_memory.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1ULL)
#define TABLE_ENTRIES 512ULL
#define ADDRESS_BITS 48U
#define CANONICAL_SIGN_BIT (1ULL << 47)
#define PHYSICAL_ADDRESS_BITS_MAX 52U
#define ENTRY_PRESENT (1ULL << 0)
#define ENTRY_WRITABLE (1ULL << 1)
#define ENTRY_USER (1ULL << 2)
#define ENTRY_WRITE_THROUGH (1ULL << 3)
#define ENTRY_CACHE_DISABLE (1ULL << 4)
#define ENTRY_LARGE (1ULL << 7)
#define ENTRY_NO_EXECUTE (1ULL << 63)
#define CR0_WRITE_PROTECT (1ULL << 16)
#define CR0_PAGING (1ULL << 31)
#define CR4_PAE (1ULL << 5)
#define CR4_LA57 (1ULL << 12)
#define EFER_LONG_MODE_ACTIVE (1ULL << 10)
#define EFER_NO_EXECUTE_ENABLE (1ULL << 11)
#define CPUID_NX (1U << 20)

struct vm_manager_state {
    volatile uint64_t *root;
    uint64_t root_physical;
    uint64_t hhdm_offset;
    uint64_t physical_address_mask;
    uint32_t physical_address_bits;
    bool initialized;
};
struct created_table {
    volatile uint64_t *parent;
    uint64_t parent_index;
    uint64_t physical_address;
};
static struct vm_manager_state manager;

static void cpu_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                      uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(0U));
}
static bool cpu_get_physical_address_bits(uint32_t *bits) {
    uint32_t eax=0U, ebx=0U, ecx=0U, edx=0U;
    if (bits == NULL) return false;
    cpu_cpuid(0x80000000U,&eax,&ebx,&ecx,&edx);
    if (eax < 0x80000008U) return false;
    cpu_cpuid(0x80000008U,&eax,&ebx,&ecx,&edx);
    *bits = eax & 0xffU;
    return *bits >= 32U && *bits <= PHYSICAL_ADDRESS_BITS_MAX;
}
static bool cpu_has_nx(void) {
    uint32_t eax=0U, ebx=0U, ecx=0U, edx=0U;
    cpu_cpuid(0x80000000U,&eax,&ebx,&ecx,&edx);
    if (eax < 0x80000001U) return false;
    cpu_cpuid(0x80000001U,&eax,&ebx,&ecx,&edx);
    return (edx & CPUID_NX) != 0U;
}
static uint64_t cpu_read_efer(void) {
    uint32_t low=0U, high=0U;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080U));
    return ((uint64_t)high << 32) | (uint64_t)low;
}
static uint64_t cpu_read_cr0(void) {
    uint64_t value=0ULL;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}
static uint64_t cpu_read_cr3(void) {
    uint64_t value=0ULL;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}
static uint64_t cpu_read_cr4(void) {
    uint64_t value=0ULL;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}
static bool address_is_canonical(uint64_t address) {
    const uint64_t upper=address >> ADDRESS_BITS;
    const bool sign=(address & CANONICAL_SIGN_BIT)!=0ULL;
    return sign ? upper==0xffffULL : upper==0ULL;
}
static volatile uint64_t *physical_to_hhdm_pointer(uint64_t physical_address) {
    uint64_t virtual_address=0ULL;
    if (manager.physical_address_mask==0ULL ||
        (physical_address & PAGE_MASK)!=0ULL ||
        (physical_address & ~manager.physical_address_mask)!=0ULL ||
        physical_address>UINT64_MAX-manager.hhdm_offset) return NULL;
    virtual_address=manager.hhdm_offset+physical_address;
    if (!address_is_canonical(virtual_address)) return NULL;
    return (volatile uint64_t *)(uintptr_t)virtual_address;
}
static void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" : :
                      "r"((void *)(uintptr_t)virtual_address) : "memory");
}

bool vm_manager_initialize(uint64_t hhdm_offset) {
    uint32_t physical_bits=0U;
    uint64_t cr0=0ULL, cr3=0ULL, cr4=0ULL, efer=0ULL;
    uint64_t root_physical=0ULL, physical_mask=0ULL;
    volatile uint64_t *root=NULL;

    if (manager.initialized || (hhdm_offset & PAGE_MASK)!=0ULL ||
        !address_is_canonical(hhdm_offset) ||
        !cpu_get_physical_address_bits(&physical_bits) || !cpu_has_nx()) {
        return false;
    }
    cr0=cpu_read_cr0(); cr3=cpu_read_cr3(); cr4=cpu_read_cr4();
    efer=cpu_read_efer();
    if ((cr0 & (CR0_PAGING|CR0_WRITE_PROTECT)) !=
            (CR0_PAGING|CR0_WRITE_PROTECT) ||
        (cr4 & CR4_PAE)==0ULL || (cr4 & CR4_LA57)!=0ULL ||
        (efer & (EFER_LONG_MODE_ACTIVE|EFER_NO_EXECUTE_ENABLE)) !=
            (EFER_LONG_MODE_ACTIVE|EFER_NO_EXECUTE_ENABLE)) return false;

    physical_mask=((1ULL << physical_bits)-1ULL) & ~PAGE_MASK;
    root_physical=cr3 & physical_mask;
    if (root_physical==0ULL) return false;

    /* Limine keeps its active page tables in bootloader-reclaimable RAM;
     * the requested Base Revision 6 HHDM maps that memory class. */
    manager.hhdm_offset=hhdm_offset;
    manager.physical_address_bits=physical_bits;
    manager.physical_address_mask=physical_mask;
    root=physical_to_hhdm_pointer(root_physical);
    if (root==NULL) {
        manager.hhdm_offset=0ULL;
        manager.physical_address_mask=0ULL;
        manager.physical_address_bits=0U;
        return false;
    }
    manager.root=root;
    manager.root_physical=root_physical;
    manager.initialized=true;
    return true;
}
static void rollback_created_tables(struct created_table *created,
                                    size_t created_count) {
    while (created_count>0U) {
        const struct created_table *item=&created[created_count-1U];
        item->parent[item->parent_index]=0ULL;
        (void)physical_allocator_free(item->physical_address);
        --created_count;
    }
    __asm__ volatile ("mfence" : : : "memory");
}
static bool get_child_table(volatile uint64_t *parent,uint64_t index,
                            bool writable,bool user,bool executable,
                            struct created_table *created,size_t *created_count,
                            volatile uint64_t **child) {
    uint64_t entry=parent[index], child_physical=0ULL;
    volatile uint64_t *child_table=NULL;
    if ((entry & ENTRY_PRESENT)==0ULL) {
        if (*created_count>=3U ||
            !physical_allocator_allocate(&child_physical)) return false;
        child_table=physical_to_hhdm_pointer(child_physical);
        if (child_table==NULL) {
            (void)physical_allocator_free(child_physical);
            return false;
        }
        for (uint64_t i=0ULL;i<TABLE_ENTRIES;i++) child_table[i]=0ULL;
        entry=child_physical|ENTRY_PRESENT|ENTRY_WRITABLE;
        if (user) entry|=ENTRY_USER;
        parent[index]=entry;
        created[*created_count].parent=parent;
        created[*created_count].parent_index=index;
        created[*created_count].physical_address=child_physical;
        ++(*created_count);
        __asm__ volatile ("mfence" : : : "memory");
        *child=child_table;
        return true;
    }
    if ((entry & ENTRY_LARGE)!=0ULL) return false;
    child_physical=entry & manager.physical_address_mask;
    if (child_physical==0ULL) return false;
    child_table=physical_to_hhdm_pointer(child_physical);
    if (child_table==NULL) return false;
    /* Do not silently loosen permissions already applied by an ancestor. */
    if ((writable && (entry & ENTRY_WRITABLE)==0ULL) ||
        (user && (entry & ENTRY_USER)==0ULL) ||
        (executable && (entry & ENTRY_NO_EXECUTE)!=0ULL)) return false;
    *child=child_table;
    return true;
}

static bool vm_map_page_internal(uint64_t virtual_address,
                                   uint64_t physical_address,
                                   uint32_t flags, bool mmio) {
    const uint32_t allowed=(uint32_t)VM_PAGE_WRITE|
                           (uint32_t)VM_PAGE_USER|
                           (uint32_t)VM_PAGE_EXECUTE;
    const bool writable=(flags & (uint32_t)VM_PAGE_WRITE)!=0U;
    const bool user=(flags & (uint32_t)VM_PAGE_USER)!=0U;
    const bool executable=(flags & (uint32_t)VM_PAGE_EXECUTE)!=0U;
    uint64_t ix[4]={0ULL,0ULL,0ULL,0ULL}, leaf=0ULL;
    volatile uint64_t *table=NULL, *next=NULL;
    struct created_table created[3]={{0}};
    size_t created_count=0U;

    if (!manager.initialized || !address_is_canonical(virtual_address) ||
        (virtual_address & PAGE_MASK)!=0ULL ||
        (physical_address & PAGE_MASK)!=0ULL || physical_address==0ULL ||
        (physical_address & ~manager.physical_address_mask)!=0ULL ||
        (flags & ~allowed)!=0U ||
        (user && (virtual_address & CANONICAL_SIGN_BIT)!=0ULL) ||
        (!mmio && !physical_allocator_is_allocated(physical_address))) return false;

    ix[0]=(virtual_address>>39)&0x1ffULL;
    ix[1]=(virtual_address>>30)&0x1ffULL;
    ix[2]=(virtual_address>>21)&0x1ffULL;
    ix[3]=(virtual_address>>12)&0x1ffULL;
    table=manager.root;
    for (size_t level=0U;level<3U;level++) {
        if (!get_child_table(table,ix[level],writable,user,executable,
                             created,&created_count,&next)) {
            rollback_created_tables(created,created_count);
            return false;
        }
        table=next;
    }
    leaf=table[ix[3]];
    if ((leaf & ENTRY_PRESENT)!=0ULL) {
        rollback_created_tables(created,created_count);
        return false;
    }
    leaf=physical_address|ENTRY_PRESENT;
    if (writable) leaf|=ENTRY_WRITABLE;
    if (user) leaf|=ENTRY_USER;
    if (!executable) leaf|=ENTRY_NO_EXECUTE;
    if (mmio) {
        /* PAT index 3 is UC under the Limine x86-64 handoff PAT layout. */
        leaf|=ENTRY_WRITE_THROUGH|ENTRY_CACHE_DISABLE;
    }
    table[ix[3]]=leaf;
    __asm__ volatile ("mfence" : : : "memory");
    invalidate_page(virtual_address);
    return true;
}

bool vm_map_page(uint64_t virtual_address,uint64_t physical_address,
                 uint32_t flags) {
    return vm_map_page_internal(virtual_address,physical_address,flags,false);
}

bool vm_map_mmio_page(uint64_t physical_address,uint64_t *virtual_address) {
    static uint64_t candidate_cursor=(1ULL<<17);
    const uint64_t pages_per_gib=1ULL<<18;
    const uint64_t search_limit=pages_per_gib*255ULL;
    uint64_t translated=0ULL;
    uint64_t hhdm_alias=0ULL;

    if (!manager.initialized || virtual_address==NULL ||
        (physical_address & PAGE_MASK)!=0ULL || physical_address==0ULL ||
        (physical_address & ~manager.physical_address_mask)!=0ULL ||
        physical_address>UINT64_MAX-manager.hhdm_offset) {
        return false;
    }

    /*
     * Limine's HHDM is WB. Refuse a second UC alias if this physical page is
     * already mapped there; x86 memory types must not conflict across aliases.
     */
    hhdm_alias=manager.hhdm_offset+physical_address;
    if (address_is_canonical(hhdm_alias) &&
        vm_translate(hhdm_alias,&translated,NULL) &&
        translated==physical_address) {
        return false;
    }

    /*
     * Search a high-half region without assuming a fixed HHDM base or
     * replacing any existing Limine mapping. Successful MMIO maps consume
     * monotonically increasing virtual pages from this search window.
     */
    for (uint64_t attempt=0ULL;attempt<search_limit;attempt++) {
        const uint64_t slot=(candidate_cursor+attempt)%search_limit;
        const uint64_t pml4=256ULL+(slot/pages_per_gib);
        const uint64_t within_gib=slot%pages_per_gib;
        const uint64_t pdpt=within_gib/512ULL;
        const uint64_t page=within_gib%512ULL;
        const uint64_t candidate=0xffff000000000000ULL|(pml4<<39)|
                                 (pdpt<<30)|(page<<12);

        if (pml4>=511ULL || !address_is_canonical(candidate) ||
            vm_translate(candidate,&translated,NULL)) {
            continue;
        }
        if (!vm_map_page_internal(candidate,physical_address,
                                  (uint32_t)VM_PAGE_WRITE,true)) {
            continue;
        }
        candidate_cursor=(slot+1ULL)%search_limit;
        *virtual_address=candidate;
        return true;
    }
    return false;
}

bool vm_unmap_page(uint64_t virtual_address) {
    uint64_t ix[4]={0ULL,0ULL,0ULL,0ULL}, entry=0ULL;
    volatile uint64_t *table=NULL;
    if (!manager.initialized || !address_is_canonical(virtual_address) ||
        (virtual_address & PAGE_MASK)!=0ULL) return false;
    ix[0]=(virtual_address>>39)&0x1ffULL;
    ix[1]=(virtual_address>>30)&0x1ffULL;
    ix[2]=(virtual_address>>21)&0x1ffULL;
    ix[3]=(virtual_address>>12)&0x1ffULL;
    table=manager.root;
    for (size_t level=0U;level<3U;level++) {
        entry=table[ix[level]];
        if ((entry & ENTRY_PRESENT)==0ULL ||
            (entry & ENTRY_LARGE)!=0ULL) return false;
        table=physical_to_hhdm_pointer(entry & manager.physical_address_mask);
        if (table==NULL) return false;
    }
    entry=table[ix[3]];
    if ((entry & ENTRY_PRESENT)==0ULL) return false;
    table[ix[3]]=0ULL;
    __asm__ volatile ("mfence" : : : "memory");
    invalidate_page(virtual_address);
    /* Intermediate tables are retained until ownership-aware reclamation exists. */
    return true;
}
bool vm_translate(uint64_t virtual_address,uint64_t *physical_address,
                  uint32_t *flags) {
    uint64_t ix[4]={0ULL,0ULL,0ULL,0ULL}, entry=0ULL;
    uint64_t page_size=PAGE_SIZE, physical_base=0ULL;
    volatile uint64_t *table=NULL;
    bool effective_write=true,effective_user=true,effective_nx=false;
    if (!manager.initialized || physical_address==NULL ||
        !address_is_canonical(virtual_address)) return false;
    ix[0]=(virtual_address>>39)&0x1ffULL;
    ix[1]=(virtual_address>>30)&0x1ffULL;
    ix[2]=(virtual_address>>21)&0x1ffULL;
    ix[3]=(virtual_address>>12)&0x1ffULL;
    table=manager.root;
    for (size_t level=0U;level<3U;level++) {
        entry=table[ix[level]];
        if ((entry & ENTRY_PRESENT)==0ULL ||
            (level==0U && (entry & ENTRY_LARGE)!=0ULL)) return false;
        effective_write=effective_write && ((entry & ENTRY_WRITABLE)!=0ULL);
        effective_user=effective_user && ((entry & ENTRY_USER)!=0ULL);
        effective_nx=effective_nx || ((entry & ENTRY_NO_EXECUTE)!=0ULL);
        if (level==1U && (entry & ENTRY_LARGE)!=0ULL) {
            page_size=1ULL<<30;
            physical_base=entry & manager.physical_address_mask & ~(page_size-1ULL);
            *physical_address=physical_base|(virtual_address&(page_size-1ULL));
            goto found;
        }
        if (level==2U && (entry & ENTRY_LARGE)!=0ULL) {
            page_size=1ULL<<21;
            physical_base=entry & manager.physical_address_mask & ~(page_size-1ULL);
            *physical_address=physical_base|(virtual_address&(page_size-1ULL));
            goto found;
        }
        table=physical_to_hhdm_pointer(entry & manager.physical_address_mask);
        if (table==NULL) return false;
    }
    entry=table[ix[3]];
    if ((entry & ENTRY_PRESENT)==0ULL) return false;
    effective_write=effective_write && ((entry & ENTRY_WRITABLE)!=0ULL);
    effective_user=effective_user && ((entry & ENTRY_USER)!=0ULL);
    effective_nx=effective_nx || ((entry & ENTRY_NO_EXECUTE)!=0ULL);
    physical_base=entry & manager.physical_address_mask;
    *physical_address=physical_base|(virtual_address&PAGE_MASK);
found:
    if (flags!=NULL) {
        uint32_t result=0U;
        if (effective_write) result|=(uint32_t)VM_PAGE_WRITE;
        if (effective_user) result|=(uint32_t)VM_PAGE_USER;
        if (!effective_nx) result|=(uint32_t)VM_PAGE_EXECUTE;
        else result|=(uint32_t)VM_PAGE_NO_EXECUTE;
        *flags=result;
    }
    return true;
}

#define USER_ADDRESS_LIMIT CANONICAL_SIGN_BIT

bool vm_user_range_valid(uint64_t virtual_address,uint64_t length,
                         bool require_write) {
    uint64_t last_address=0ULL;
    uint64_t page=0ULL;
    uint64_t last_page=0ULL;
    if (length==0ULL) return true;
    if (virtual_address==0ULL || virtual_address>=USER_ADDRESS_LIMIT ||
        length>USER_ADDRESS_LIMIT-virtual_address) return false;
    last_address=virtual_address+length-1ULL;
    page=virtual_address&~PAGE_MASK;
    last_page=last_address&~PAGE_MASK;
    for (;;) {
        uint64_t physical=0ULL;
        uint32_t flags=0U;
        if (!vm_translate(page,&physical,&flags) ||
            (flags&(uint32_t)VM_PAGE_USER)==0U ||
            (require_write && (flags&(uint32_t)VM_PAGE_WRITE)==0U)) {
            return false;
        }
        if (page==last_page) break;
        if (page>UINT64_MAX-PAGE_SIZE) return false;
        page+=PAGE_SIZE;
    }
    return true;
}

bool vm_copy_from_user(void *destination,uint64_t user_source,size_t length) {
    size_t copied=0U;
    if (length==0U) return true;
    if (destination==NULL ||
        !vm_user_range_valid(user_source,(uint64_t)length,false)) return false;
    while (copied<length) {
        uint64_t physical=0ULL;
        const uint64_t source=user_source+(uint64_t)copied;
        const uint64_t offset=source&PAGE_MASK;
        size_t chunk=(size_t)(PAGE_SIZE-offset);
        volatile uint8_t *mapped=NULL;
        uint8_t *target=(uint8_t *)destination+copied;
        if (!vm_translate(source,&physical,NULL)) return false;
        if (chunk>length-copied) chunk=length-copied;
        mapped=(volatile uint8_t *)physical_to_hhdm_pointer(physical&~PAGE_MASK);
        if (mapped==NULL) return false;
        for (size_t index=0U;index<chunk;index++) target[index]=mapped[offset+index];
        copied+=chunk;
    }
    return true;
}

bool vm_copy_to_user(uint64_t user_destination,const void *source,size_t length) {
    size_t copied=0U;
    if (length==0U) return true;
    if (source==NULL ||
        !vm_user_range_valid(user_destination,(uint64_t)length,true)) return false;
    while (copied<length) {
        uint64_t physical=0ULL;
        const uint64_t destination=user_destination+(uint64_t)copied;
        const uint64_t offset=destination&PAGE_MASK;
        size_t chunk=(size_t)(PAGE_SIZE-offset);
        volatile uint8_t *mapped=NULL;
        const uint8_t *input=(const uint8_t *)source+copied;
        if (!vm_translate(destination,&physical,NULL)) return false;
        if (chunk>length-copied) chunk=length-copied;
        mapped=(volatile uint8_t *)physical_to_hhdm_pointer(physical&~PAGE_MASK);
        if (mapped==NULL) return false;
        for (size_t index=0U;index<chunk;index++) mapped[offset+index]=input[index];
        copied+=chunk;
    }
    return true;
}

#ifdef DAWN_KERNEL_TEST
static bool find_and_map_test_page(uint64_t physical_address,uint32_t flags,
                                   uint64_t *virtual_address) {
    uint64_t translated=0ULL;
    if (virtual_address==NULL) return false;

    /*
     * BIOS and UEFI firmware can leave different lower-half mappings behind.
     * Search for an actually unmapped page instead of assuming a completely
     * empty PML4 slot. vm_map_page performs the final ancestor-permission checks.
     */
    for (uint64_t pml4=1ULL;pml4<256ULL;pml4++) {
        const uint64_t root_entry=manager.root[pml4];
        if ((root_entry & ENTRY_PRESENT)!=0ULL &&
            ((root_entry & ENTRY_WRITABLE)==0ULL ||
             (((flags&(uint32_t)VM_PAGE_USER)!=0U) &&
              (root_entry & ENTRY_USER)==0ULL))) continue;
        for (uint64_t pdpt=0ULL;pdpt<TABLE_ENTRIES;pdpt++) {
            const uint64_t candidate=(pml4<<39)|(pdpt<<30)|0x12345000ULL;
            if (vm_translate(candidate,&translated,NULL)) continue;
            if (vm_map_page(candidate,physical_address,flags)) {
                *virtual_address=candidate;
                return true;
            }
        }
    }
    return false;
}

bool vm_manager_self_test(void) {
    uint64_t va=0ULL,code_va=0ULL,data=0ULL,code=0ULL,translated=UINT64_MAX;
    uint64_t mmio_va=0ULL;
    uint32_t flags=0U;
    if (!manager.initialized || !physical_allocator_allocate(&data)) {
        return false;
    }

    if (!find_and_map_test_page(data,(uint32_t)VM_PAGE_WRITE,&va) ||
        vm_map_page(va,data,(uint32_t)VM_PAGE_WRITE) ||
        !vm_translate(va,&translated,&flags) || translated!=data ||
        (flags&(uint32_t)VM_PAGE_WRITE)==0U ||
        (flags&(uint32_t)VM_PAGE_NO_EXECUTE)==0U ||
        (flags&(uint32_t)VM_PAGE_USER)!=0U ||
        vm_map_page(va+1ULL,data,0U) ||
        vm_map_page(va,0ULL,0U) ||
        vm_map_page(CANONICAL_SIGN_BIT,data,0U) ||
        !vm_unmap_page(va) || vm_translate(va,&translated,&flags)) {
        (void)vm_unmap_page(va);
        (void)physical_allocator_free(data);
        return false;
    }
    if (!physical_allocator_free(data)) return false;

    if (!physical_allocator_allocate(&code)) return false;
    if (!find_and_map_test_page(code,(uint32_t)VM_PAGE_EXECUTE,&code_va) ||
        !vm_translate(code_va,&translated,&flags) || translated!=code ||
        (flags&(uint32_t)VM_PAGE_EXECUTE)==0U ||
        (flags&(uint32_t)VM_PAGE_WRITE)!=0U ||
        (flags&(uint32_t)VM_PAGE_NO_EXECUTE)!=0U ||
        vm_user_range_valid(code_va,1ULL,false) ||
        vm_copy_from_user(&flags,code_va,sizeof(uint32_t)) ||
        vm_copy_to_user(code_va,&flags,sizeof(uint32_t)) ||
        !vm_unmap_page(code_va) ||
        !physical_allocator_free(code)) {
        (void)vm_unmap_page(code_va);
        (void)physical_allocator_free(code);
        return false;
    }

    uint64_t user_data=0ULL,user_va=0ULL;
    volatile uint8_t *user_alias=NULL;
    uint8_t copied[8]={0U,0U,0U,0U,0U,0U,0U,0U};
    const uint8_t seed[8]={0x44U,0x41U,0x57U,0x4eU,0x2dU,0x4fU,0x53U,0x21U};
    const uint8_t update[4]={0x55U,0x53U,0x45U,0x52U};
    if (!physical_allocator_allocate(&user_data) ||
        !find_and_map_test_page(user_data,
             (uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE,&user_va)) {
        (void)physical_allocator_free(user_data);
        return false;
    }
    user_alias=(volatile uint8_t *)physical_to_hhdm_pointer(user_data);
    if (user_alias==NULL) {
        (void)vm_unmap_page(user_va);
        (void)physical_allocator_free(user_data);
        return false;
    }
    for (size_t index=0U;index<sizeof(seed);index++) user_alias[index]=seed[index];
    if (!vm_user_range_valid(user_va+1ULL,6ULL,false) ||
        !vm_user_range_valid(user_va+1ULL,6ULL,true) ||
        !vm_copy_from_user(copied,user_va+1ULL,7U) ||
        copied[0]!=seed[1] || copied[1]!=seed[2] ||
        copied[2]!=seed[3] || copied[3]!=seed[4] ||
        copied[4]!=seed[5] || copied[5]!=seed[6] ||
        copied[6]!=seed[7] ||
        !vm_copy_to_user(user_va+8ULL,update,sizeof(update)) ||
        user_alias[8]!=update[0] || user_alias[9]!=update[1] ||
        user_alias[10]!=update[2] || user_alias[11]!=update[3] ||
        vm_user_range_valid(0ULL,1ULL,false) ||
        vm_user_range_valid(USER_ADDRESS_LIMIT-4ULL,8ULL,false) ||
        !vm_user_range_valid(0ULL,0ULL,false) ||
        !vm_copy_from_user(NULL,0ULL,0U) ||
        !vm_copy_to_user(0ULL,NULL,0U)) {
        (void)vm_unmap_page(user_va);
        (void)physical_allocator_free(user_data);
        return false;
    }
    if (!vm_unmap_page(user_va) ||
        vm_user_range_valid(user_va,1ULL,false) ||
        !physical_allocator_free(user_data)) return false;

    if (!vm_map_mmio_page(0xfee00000ULL,&mmio_va) ||
        !vm_translate(mmio_va,&translated,&flags) ||
        translated!=0xfee00000ULL ||
        (flags&(uint32_t)VM_PAGE_NO_EXECUTE)==0U ||
        !vm_unmap_page(mmio_va)) {
        (void)vm_unmap_page(mmio_va);
        return false;
    }
    return true;
}
#endif


/*
 * Process address-space roots share only the supervisor half of the kernel
 * PML4. The lower half starts empty and is owned by the address-space object.
 * User data frames remain caller-owned; destroy releases page-table frames only.
 * Activation is intentionally deferred until interrupt/syscall stack switching
 * and process lifetime management are ready.
 */
struct vm_address_space {
    uint64_t root_physical;
};

static volatile uint64_t *address_space_root(const struct vm_address_space *space) {
    if (space==NULL || space->root_physical==0ULL) return NULL;
    return physical_to_hhdm_pointer(space->root_physical);
}

bool vm_address_space_create(struct vm_address_space *space) {
    uint64_t root_physical=0ULL;
    volatile uint64_t *root=NULL;
    if (!manager.initialized || space==NULL || space->root_physical!=0ULL ||
        !physical_allocator_allocate(&root_physical)) return false;
    root=physical_to_hhdm_pointer(root_physical);
    if (root==NULL) {
        (void)physical_allocator_free(root_physical);
        return false;
    }
    for (uint64_t i=0ULL;i<TABLE_ENTRIES;i++) {
        root[i]=(i>=256ULL) ? (manager.root[i]&~ENTRY_USER) : 0ULL;
    }
    __asm__ volatile ("mfence" : : : "memory");
    space->root_physical=root_physical;
    return true;
}

bool vm_address_space_map_user(struct vm_address_space *space,
                               uint64_t virtual_address,
                               uint64_t physical_address,uint32_t flags) {
    const uint32_t allowed=(uint32_t)VM_PAGE_WRITE|
                           (uint32_t)VM_PAGE_USER|
                           (uint32_t)VM_PAGE_EXECUTE;
    uint64_t ix[4]={0ULL,0ULL,0ULL,0ULL};
    volatile uint64_t *table=address_space_root(space),*next=NULL;
    struct created_table created[3]={{0}};
    size_t created_count=0U;
    if (table==NULL || virtual_address==0ULL ||
        !address_is_canonical(virtual_address) ||
        (virtual_address&CANONICAL_SIGN_BIT)!=0ULL ||
        (virtual_address&PAGE_MASK)!=0ULL ||
        (physical_address&PAGE_MASK)!=0ULL || physical_address==0ULL ||
        (physical_address&~manager.physical_address_mask)!=0ULL ||
        (flags&~allowed)!=0U || (flags&(uint32_t)VM_PAGE_USER)==0U ||
        !physical_allocator_is_allocated(physical_address)) return false;
    ix[0]=(virtual_address>>39)&0x1ffULL;
    ix[1]=(virtual_address>>30)&0x1ffULL;
    ix[2]=(virtual_address>>21)&0x1ffULL;
    ix[3]=(virtual_address>>12)&0x1ffULL;
    for (size_t level=0U;level<3U;level++) {
        if (!get_child_table(table,ix[level],
                (flags&(uint32_t)VM_PAGE_WRITE)!=0U,true,
                (flags&(uint32_t)VM_PAGE_EXECUTE)!=0U,
                created,&created_count,&next)) {
            rollback_created_tables(created,created_count);
            return false;
        }
        table=next;
    }
    if ((table[ix[3]]&ENTRY_PRESENT)!=0ULL) {
        rollback_created_tables(created,created_count);
        return false;
    }
    table[ix[3]]=physical_address|ENTRY_PRESENT|ENTRY_USER|
        ((flags&(uint32_t)VM_PAGE_WRITE)!=0U ? ENTRY_WRITABLE : 0ULL)|
        ((flags&(uint32_t)VM_PAGE_EXECUTE)==0U ? ENTRY_NO_EXECUTE : 0ULL);
    __asm__ volatile ("mfence" : : : "memory");
    return true;
}

bool vm_address_space_translate(const struct vm_address_space *space,
                                uint64_t virtual_address,
                                uint64_t *physical_address,uint32_t *flags) {
    uint64_t ix[4]={0ULL,0ULL,0ULL,0ULL},entry=0ULL;
    bool writable=true,user=true,nx=false;
    volatile uint64_t *table=address_space_root(space);
    if (table==NULL || physical_address==NULL ||
        !address_is_canonical(virtual_address)) return false;
    ix[0]=(virtual_address>>39)&0x1ffULL;
    ix[1]=(virtual_address>>30)&0x1ffULL;
    ix[2]=(virtual_address>>21)&0x1ffULL;
    ix[3]=(virtual_address>>12)&0x1ffULL;
    for (size_t level=0U;level<3U;level++) {
        entry=table[ix[level]];
        if ((entry&ENTRY_PRESENT)==0ULL || (entry&ENTRY_LARGE)!=0ULL)
            return false;
        writable=writable&&((entry&ENTRY_WRITABLE)!=0ULL);
        user=user&&((entry&ENTRY_USER)!=0ULL);
        nx=nx||((entry&ENTRY_NO_EXECUTE)!=0ULL);
        table=physical_to_hhdm_pointer(entry&manager.physical_address_mask);
        if (table==NULL) return false;
    }
    entry=table[ix[3]];
    if ((entry&ENTRY_PRESENT)==0ULL) return false;
    writable=writable&&((entry&ENTRY_WRITABLE)!=0ULL);
    user=user&&((entry&ENTRY_USER)!=0ULL);
    nx=nx||((entry&ENTRY_NO_EXECUTE)!=0ULL);
    *physical_address=(entry&manager.physical_address_mask)|
                      (virtual_address&PAGE_MASK);
    if (flags!=NULL) {
        *flags=(writable?(uint32_t)VM_PAGE_WRITE:0U)|
               (user?(uint32_t)VM_PAGE_USER:0U)|
               (nx?(uint32_t)VM_PAGE_NO_EXECUTE:(uint32_t)VM_PAGE_EXECUTE);
    }
    return true;
}

static bool address_space_free_lower_tables(volatile uint64_t *table,
                                            unsigned int level) {
    for (uint64_t i=0ULL;i<TABLE_ENTRIES;i++) {
        const uint64_t entry=table[i];
        uint64_t child_physical=0ULL;
        volatile uint64_t *child=NULL;
        if ((entry&ENTRY_PRESENT)==0ULL) continue;
        if (level==3U) {
            if ((entry&ENTRY_LARGE)!=0ULL) return false;
            continue; /* Leaf data frames are caller-owned. */
        }
        if ((entry&ENTRY_LARGE)!=0ULL) return false;
        child_physical=entry&manager.physical_address_mask;
        child=physical_to_hhdm_pointer(child_physical);
        if (child==NULL) return false;
        if (!address_space_free_lower_tables(child,level+1U)) return false;
        table[i]=0ULL;
        if (!physical_allocator_free(child_physical)) return false;
    }
    return true;
}

bool vm_address_space_destroy(struct vm_address_space *space) {
    volatile uint64_t *root=address_space_root(space);
    const uint64_t active=cpu_read_cr3()&manager.physical_address_mask;
    if (root==NULL || active==space->root_physical) return false;
    /* Only lower-half tables are private. Upper-half kernel tables are shared. */
    for (uint64_t i=0ULL;i<256ULL;i++) {
        const uint64_t entry=root[i];
        uint64_t child_physical=0ULL;
        volatile uint64_t *child=NULL;
        if ((entry&ENTRY_PRESENT)==0ULL) continue;
        if ((entry&ENTRY_LARGE)!=0ULL) return false;
        child_physical=entry&manager.physical_address_mask;
        child=physical_to_hhdm_pointer(child_physical);
        if (child==NULL || !address_space_free_lower_tables(child,1U))
            return false;
        root[i]=0ULL;
        if (!physical_allocator_free(child_physical)) return false;
    }
    if (!physical_allocator_free(space->root_physical)) return false;
    space->root_physical=0ULL;
    return true;
}

#ifdef DAWN_KERNEL_TEST
bool vm_address_space_self_test(void) {
    struct vm_address_space first={0},second={0};
    uint64_t page=0ULL,translated=0ULL;
    uint32_t flags=0U;
    const uint64_t user_va=0x0000004000000000ULL;
    if (!physical_allocator_allocate(&page) ||
        !vm_address_space_create(&first) ||
        !vm_address_space_create(&second)) goto fail;
    if (first.root_physical==second.root_physical ||
        !vm_address_space_map_user(&first,user_va,page,
            (uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE) ||
        !vm_address_space_translate(&first,user_va,&translated,&flags) ||
        translated!=page ||
        (flags&((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE))!=
            ((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE) ||
        vm_address_space_translate(&second,user_va,&translated,NULL) ||
        vm_translate(user_va,&translated,NULL)) goto fail;
    if (!vm_address_space_destroy(&first) ||
        !vm_address_space_destroy(&second) ||
        !physical_allocator_free(page)) return false;
    return true;
fail:
    if (first.root_physical!=0ULL) (void)vm_address_space_destroy(&first);
    if (second.root_physical!=0ULL) (void)vm_address_space_destroy(&second);
    if (page!=0ULL && physical_allocator_is_allocated(page))
        (void)physical_allocator_free(page);
    return false;
}
#endif
