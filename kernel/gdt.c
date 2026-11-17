/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"

#define GDT_ENTRY_COUNT 7U
#define PRIVILEGE_STACK_SIZE 16384U
#define TSS_TYPE_AVAILABLE_64 0x9ULL
#define TSS_TYPE_BUSY_64 0x0bULL

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss64)==104U,"64-bit TSS must be 104 bytes");
_Static_assert(sizeof(struct gdtr)==10U,"64-bit GDTR must be 10 bytes");

extern void dawn_gdt_load(const struct gdtr *descriptor);
static uint64_t gdt[GDT_ENTRY_COUNT] __attribute__((aligned(16)));
static struct tss64 tss __attribute__((aligned(16)));
static uint8_t privilege_stack[PRIVILEGE_STACK_SIZE] __attribute__((aligned(16)));
static bool initialized;

static bool canonical_address(uint64_t address) {
    const uint64_t upper=address>>48;
    const uint64_t expected=((address>>47)&1ULL)!=0ULL ? 0xffffULL : 0ULL;
    return address!=0ULL && upper==expected;
}
static uint64_t segment_descriptor(uint8_t access,uint8_t flags) {
    return 0x000000000000ffffULL |
           ((uint64_t)access<<40) |
           ((uint64_t)(flags&0x0fU)<<52);
}
static void install_tss_descriptor(uint64_t base,uint32_t limit) {
    const uint64_t low=(uint64_t)(limit&0xffffU) |
        ((base&0xffffffULL)<<16) |
        (0x89ULL<<40) |
        ((uint64_t)((limit>>16)&0x0fU)<<48) |
        (((base>>24)&0xffULL)<<56);
    gdt[5]=low;
    gdt[6]=base>>32;
}

bool gdt_tss_set_kernel_stack(uint64_t stack_top) {
    if (!initialized || !canonical_address(stack_top) ||
        (stack_top&0x0fULL)!=0ULL) return false;
    tss.rsp0=stack_top;
    __asm__ volatile ("mfence" : : : "memory");
    return true;
}
uint64_t gdt_tss_kernel_stack_top(void) {
    return initialized ? tss.rsp0 : 0ULL;
}

bool gdt_tss_initialize(void) {
    struct gdtr descriptor={0};
    const uint64_t stack_top=(uint64_t)(uintptr_t)(privilege_stack+sizeof(privilege_stack));
    uint16_t task_register=0U;
    if (initialized || !canonical_address(stack_top) || (stack_top&0x0fULL)!=0ULL)
        return false;

    gdt[0]=0ULL;
    gdt[1]=segment_descriptor(0x9aU,0x0aU);
    gdt[2]=segment_descriptor(0x92U,0x0cU);
    gdt[3]=segment_descriptor(0xf2U,0x0cU);
    gdt[4]=segment_descriptor(0xfaU,0x0aU);

    tss=(struct tss64){0};
    tss.rsp0=stack_top;
    tss.iomap_base=(uint16_t)sizeof(tss);
    install_tss_descriptor((uint64_t)(uintptr_t)&tss,(uint32_t)(sizeof(tss)-1U));

    descriptor.limit=(uint16_t)(sizeof(gdt)-1U);
    descriptor.base=(uint64_t)(uintptr_t)&gdt[0];
    dawn_gdt_load(&descriptor);
    {
        const uint16_t selector=(uint16_t)DAWN_TSS_SELECTOR;
        __asm__ volatile ("ltr %0" : : "r"(selector) : "memory");
    }
    __asm__ volatile ("str %0" : "=r"(task_register));
    if (task_register!=(uint16_t)DAWN_TSS_SELECTOR ||
        tss.rsp0!=stack_top || tss.iomap_base!=(uint16_t)sizeof(tss))
        return false;
    initialized=true;
    return true;
}

#ifdef DAWN_KERNEL_TEST
bool gdt_tss_self_test(void) {
    const uint64_t original=tss.rsp0;
    const uint64_t temporary=original-16ULL;
    uint16_t selector=0U;
    __asm__ volatile ("str %0" : "=r"(selector));
    if (!initialized || selector!=(uint16_t)DAWN_TSS_SELECTOR ||
        ((gdt[5]>>40)&0x0fULL)!=TSS_TYPE_BUSY_64 ||
        ((gdt[3]>>45)&0x03ULL)!=0x03ULL ||
        ((gdt[4]>>45)&0x03ULL)!=0x03ULL ||
        (gdt[5]&(1ULL<<47))==0ULL ||
        !canonical_address(original) || (original&0x0fULL)!=0ULL ||
        !gdt_tss_set_kernel_stack(temporary) ||
        gdt_tss_kernel_stack_top()!=temporary ||
        gdt_tss_set_kernel_stack(0ULL) ||
        gdt_tss_set_kernel_stack(0x0000800000000000ULL) ||
        gdt_tss_set_kernel_stack(original+1ULL) ||
        !gdt_tss_set_kernel_stack(original) ||
        gdt_tss_kernel_stack_top()!=original) return false;
    return true;
}
#endif
