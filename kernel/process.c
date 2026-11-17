/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "page_table.h"
#include "process.h"

#define CPU_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

struct process_record {
    struct vm_address_space address_space;
    uint64_t id;
    uint64_t kernel_stack_top;
    enum process_state state;
};

static struct process_record processes[DAWN_PROCESS_MAX];
static uint64_t next_process_id;
static bool initialized;

static inline uint64_t read_rflags(void) {
    uint64_t flags=0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}
static inline uint16_t read_cs(void) {
    uint16_t selector=0U;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}
static inline void disable_interrupts(void) {
    __asm__ volatile ("cli" : : : "memory");
}
static inline void restore_interrupt_flag(uint64_t flags) {
    if ((flags & CPU_RFLAGS_INTERRUPT_ENABLE)!=0ULL)
        __asm__ volatile ("sti" : : : "memory");
}
static struct process_record *find_process(uint64_t id) {
    if (id==0ULL) return NULL;
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++)
        if (processes[i].state!=PROCESS_STATE_FREE &&
            processes[i].id==id) return &processes[i];
    return NULL;
}

bool process_manager_initialize(void) {
    const uint64_t flags=read_rflags();
    if (initialized || (read_cs()&3U)!=0U ||
        gdt_tss_kernel_stack_top()==0ULL) return false;
    disable_interrupts();
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++)
        processes[i]=(struct process_record){0};
    next_process_id=1ULL;
    initialized=true;
    restore_interrupt_flag(flags);
    return true;
}

bool process_create(uint64_t *process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *slot=NULL;
    struct vm_address_space space={0};
    uint64_t stack_top=0ULL;
    if (!initialized || process_id==NULL || *process_id!=0ULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++) {
        if (processes[i].state==PROCESS_STATE_FREE) {
            slot=&processes[i];
            break;
        }
    }
    if (slot==NULL || next_process_id==0ULL ||
        next_process_id==UINT64_MAX ||
        !vm_address_space_create(&space)) {
        restore_interrupt_flag(flags);
        return false;
    }
    if (!gdt_tss_allocate_kernel_stack(&stack_top)) {
        (void)vm_address_space_destroy(&space);
        restore_interrupt_flag(flags);
        return false;
    }
    slot->address_space=space;
    slot->kernel_stack_top=stack_top;
    slot->id=next_process_id++;
    slot->state=PROCESS_STATE_CREATED;
    *process_id=slot->id;
    restore_interrupt_flag(flags);
    return true;
}

bool process_mark_ready(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_CREATED) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->state=PROCESS_STATE_READY;
    restore_interrupt_flag(flags);
    return true;
}

bool process_mark_exited(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL ||
        (record->state!=PROCESS_STATE_CREATED &&
         record->state!=PROCESS_STATE_READY)) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->state=PROCESS_STATE_EXITED;
    restore_interrupt_flag(flags);
    return true;
}

bool process_get_info(uint64_t process_id,struct process_info *info) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || info==NULL || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL) {
        restore_interrupt_flag(flags);
        return false;
    }
    info->id=record->id;
    info->state=record->state;
    info->address_space_root=record->address_space.root_physical;
    info->kernel_stack_top=record->kernel_stack_top;
    restore_interrupt_flag(flags);
    return true;
}

bool process_reap(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_EXITED) {
        restore_interrupt_flag(flags);
        return false;
    }
    /* VM teardown refuses to free the currently active CR3 root. */
    if (record->address_space.root_physical!=0ULL &&
        !vm_address_space_destroy(&record->address_space)) {
        restore_interrupt_flag(flags);
        return false;
    }
    if (record->kernel_stack_top!=0ULL &&
        !gdt_tss_release_kernel_stack(record->kernel_stack_top)) {
        restore_interrupt_flag(flags);
        return false;
    }
    *record=(struct process_record){0};
    restore_interrupt_flag(flags);
    return true;
}

#ifdef DAWN_KERNEL_TEST
bool process_manager_self_test(void) {
    uint64_t ids[DAWN_PROCESS_MAX]={0ULL};
    struct process_info info={0};
    uint64_t extra=0ULL;
    if (!initialized || !process_create(NULL) ||
        process_mark_ready(0ULL) || process_mark_exited(0ULL)) return false;
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++) {
        if (!process_create(&ids[i]) || ids[i]==0ULL) return false;
        if (!process_get_info(ids[i],&info) ||
            info.id!=ids[i] || info.state!=PROCESS_STATE_CREATED ||
            info.address_space_root==0ULL || info.kernel_stack_top==0ULL)
            return false;
        for (size_t j=0U;j<i;j++)
            if (ids[i]==ids[j]) return false;
    }
    if (process_create(&extra) || extra!=0ULL ||
        process_mark_ready(ids[0])==false ||
        process_mark_ready(ids[0]) ||
        process_reap(ids[0]) ||
        !process_mark_exited(ids[0]) ||
        process_mark_exited(ids[0]) ||
        !process_reap(ids[0]) ||
        process_get_info(ids[0],&info)) return false;
    for (size_t i=1U;i<DAWN_PROCESS_MAX;i++) {
        if (!process_mark_exited(ids[i]) || !process_reap(ids[i]) ||
            process_get_info(ids[i],&info)) return false;
    }
    if (!process_create(&extra) || extra<=ids[DAWN_PROCESS_MAX-1U] ||
        !process_mark_exited(extra) || !process_reap(extra)) return false;
    return true;
}
#endif
