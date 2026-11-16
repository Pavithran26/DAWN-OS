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

bool vm_map_page(uint64_t virtual_address,uint64_t physical_address,
                 uint32_t flags) {
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
        (physical_address & PAGE_MASK)!=0ULL ||
        (physical_address & ~manager.physical_address_mask)!=0ULL ||
        (flags & ~allowed)!=0U ||
        (user && (virtual_address & CANONICAL_SIGN_BIT)!=0ULL) ||
        !physical_allocator_is_allocated(physical_address)) return false;

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
    table[ix[3]]=leaf;
    __asm__ volatile ("mfence" : : : "memory");
    invalidate_page(virtual_address);
    return true;
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
            (root_entry & ENTRY_WRITABLE)==0ULL) continue;
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
        !vm_unmap_page(code_va) ||
        !physical_allocator_free(code)) {
        (void)vm_unmap_page(code_va);
        (void)physical_allocator_free(code);
        return false;
    }
    return true;
}
#endif
