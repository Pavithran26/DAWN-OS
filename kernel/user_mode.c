/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stdint.h>
#include "gdt.h"
#include "page_table.h"
#include "user_mode.h"
#define USER_LIMIT (1ULL << 47)
#define USER_SELECTOR_RPL3 3ULL
#define USER_CODE_SELECTOR 0x20ULL
#define USER_DATA_SELECTOR 0x18ULL
extern _Noreturn void arch_enter_user_mode(uint64_t entry,uint64_t stack_top);
bool user_mode_validate_entry(uint64_t entry,uint64_t stack_top) {
    uint64_t physical=0ULL; uint32_t entry_flags=0U,stack_flags=0U;
    if (gdt_tss_kernel_stack_top()==0ULL || entry==0ULL || entry>=USER_LIMIT ||
        stack_top==0ULL || stack_top>=USER_LIMIT || (stack_top&0x0fULL)!=0ULL) return false;
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
    if (user_mode_validate_entry(0ULL,0ULL) ||
        user_mode_validate_entry(USER_LIMIT,0x1000ULL) ||
        user_mode_validate_entry(0x1000ULL,USER_LIMIT) ||
        user_mode_validate_entry(0x1000ULL,0x1001ULL)) return false;
    return true;
}
#endif
