/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stdint.h>
#include "gdt.h"
#include "page_table.h"
#include "process.h"
#include "physical_memory.h"
#include "user_mode.h"
#define USER_LIMIT (1ULL << 47)
#define USER_SELECTOR_RPL3 3ULL
#define USER_CODE_SELECTOR 0x20ULL
#define USER_DATA_SELECTOR 0x18ULL
#define USER_TEST_CODE_ADDRESS 0x40000000ULL
#define USER_TEST_STACK_ADDRESS 0x40001000ULL
#define CPU_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)
extern _Noreturn void arch_enter_user_mode(uint64_t entry,uint64_t stack_top);

static inline uint64_t user_mode_read_rflags(void) {
    uint64_t flags=0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static bool user_mode_launch_context_valid(void) {
    const uint64_t flags=user_mode_read_rflags();
    struct process_info info={0};
    struct vm_address_space active_space={0};
    uint64_t process_id=0ULL;
    uint16_t cs=0U;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    if ((cs&3U)!=0U || (flags&CPU_RFLAGS_INTERRUPT_ENABLE)!=0ULL)
        return false;
    process_id=process_active_id();
    if (process_id==0ULL || !process_get_info(process_id,&info) ||
        info.state!=PROCESS_STATE_READY ||
        info.kernel_stack_top!=gdt_tss_kernel_stack_top())
        return false;
    active_space.root_physical=info.address_space_root;
    return vm_address_space_is_active(&active_space);
}
bool user_mode_validate_entry(uint64_t entry,uint64_t stack_top) {
    uint64_t physical=0ULL; uint32_t entry_flags=0U,stack_flags=0U;
    if (!user_mode_launch_context_valid() ||
        gdt_tss_kernel_stack_top()==0ULL || entry==0ULL || entry>=USER_LIMIT ||
        stack_top==0ULL || stack_top>=USER_LIMIT || (stack_top&0x0fULL)!=0ULL)
        return false;
    if (!vm_translate(entry,&physical,&entry_flags) ||
        (entry_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (entry_flags&(uint32_t)VM_PAGE_EXECUTE)==0U ||
        (entry_flags&(uint32_t)VM_PAGE_NO_EXECUTE)!=0U) return false;
    if (!vm_translate(stack_top-1ULL,&physical,&stack_flags) ||
        (stack_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (stack_flags&(uint32_t)VM_PAGE_WRITE)==0U) return false;
    return (USER_CODE_SELECTOR&3ULL)==USER_SELECTOR_RPL3 &&
           (USER_DATA_SELECTOR&3ULL)==USER_SELECTOR_RPL3;
}
_Noreturn void user_mode_enter(uint64_t entry,uint64_t stack_top) {
    if (!user_mode_validate_entry(entry,stack_top)) {
        __asm__ volatile ("cli" : : : "memory");
        for (;;) __asm__ volatile ("hlt" : : : "memory");
    }
    arch_enter_user_mode(entry,stack_top);
}
#ifdef DAWN_KERNEL_TEST
bool user_mode_self_test(void) {
    const uint64_t saved_flags=user_mode_read_rflags();
    uint64_t process_id=0ULL,code_physical=0ULL,stack_physical=0ULL;
    struct process_info info={0};
    struct vm_address_space space={0};
    bool created=false,code_allocated=false,stack_allocated=false;
    bool active=false,reaped=false,result=false;
    __asm__ volatile ("cli" : : : "memory");

    if (user_mode_validate_entry(0ULL,0ULL) ||
        user_mode_validate_entry(USER_LIMIT,0x1000ULL) ||
        user_mode_validate_entry(0x1000ULL,USER_LIMIT) ||
        user_mode_validate_entry(0x1000ULL,0x1001ULL))
        goto cleanup;

    if (!physical_allocator_allocate(&code_physical)) goto cleanup;
    code_allocated=true;
    if (!physical_allocator_allocate(&stack_physical)) goto cleanup;
    stack_allocated=true;
    if (!process_create(&process_id)) goto cleanup;
    created=true;
    if (!process_mark_ready(process_id) ||
        !process_get_info(process_id,&info)) goto cleanup;
    space.root_physical=info.address_space_root;
    if (!vm_address_space_map_user(&space,USER_TEST_CODE_ADDRESS,
            code_physical,(uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_EXECUTE) ||
        !vm_address_space_map_user(&space,USER_TEST_STACK_ADDRESS,
            stack_physical,(uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE))
        goto cleanup;
    if (!process_activate(process_id)) goto cleanup;
    active=true;

    result=user_mode_validate_entry(USER_TEST_CODE_ADDRESS+16ULL,
                                     USER_TEST_STACK_ADDRESS+4096ULL) &&
           !user_mode_validate_entry(USER_TEST_STACK_ADDRESS+16ULL,
                                     USER_TEST_STACK_ADDRESS+4096ULL) &&
           !user_mode_validate_entry(USER_TEST_CODE_ADDRESS+16ULL,
                                     USER_TEST_STACK_ADDRESS+4095ULL);
cleanup:
    if (active) {
        if (!process_deactivate()) result=false;
        else active=false;
    }
    if (created && !active) {
        if (process_mark_exited(process_id) &&
            process_reap(process_id)) reaped=true;
        else result=false;
    }
    /* User frames remain allocated if a process root could not be reaped. */
    if (!created || reaped) {
        if (stack_allocated && !physical_allocator_free(stack_physical))
            result=false;
        if (code_allocated && !physical_allocator_free(code_physical))
            result=false;
    }
    if ((saved_flags&CPU_RFLAGS_INTERRUPT_ENABLE)!=0ULL)
        __asm__ volatile ("sti" : : : "memory");
    return result && !active && (!created || reaped) &&
           (!created || process_active_id()!=process_id);
}
#endif
