/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "page_table.h"
#include "physical_memory.h"
#include "process.h"
#include "elf_loader.h"
#include "scheduler.h"
#include "vfs.h"

extern uint64_t dawn_syscall_kernel_stack_top;

#define CPU_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

struct process_user_page {
    uint64_t virtual_address;
    uint64_t physical_address;
};

struct process_record {
    struct vm_address_space address_space;
    struct process_user_page user_pages[DAWN_PROCESS_MAX_USER_PAGES];
    size_t user_page_count;
    struct dawn_fd_entry fds[DAWN_PROCESS_MAX_FDS];
    char cwd[VFS_MAX_PATH];
    uint64_t heap_start;
    uint64_t heap_break;
    uint64_t id;
    uint64_t parent_id;
    uint64_t kernel_stack_top;
    int64_t exit_status;
    enum process_state state;
};

static struct process_record processes[DAWN_PROCESS_MAX];
static uint64_t next_process_id;
static uint64_t bootstrap_kernel_stack_top;
static uint64_t active_process_identifier;
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
    if (!vm_address_space_activate_kernel()) {
        restore_interrupt_flag(flags);
        return false;
    }
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++)
        processes[i]=(struct process_record){0};
    next_process_id=1ULL;
    bootstrap_kernel_stack_top=gdt_tss_kernel_stack_top();
    active_process_identifier=0ULL;
    initialized=true;
    restore_interrupt_flag(flags);
    return true;
}

bool process_create(uint64_t *process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *slot=NULL;
    struct process_record *parent=NULL;
    struct vm_address_space space={0};
    uint64_t stack_top=0ULL;
    if (!initialized || process_id==NULL || *process_id!=0ULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    if (active_process_identifier!=0ULL) {
        parent=find_process(active_process_identifier);
        if (parent==NULL || parent->state!=PROCESS_STATE_READY) {
            restore_interrupt_flag(flags);
            return false;
        }
    }
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
    slot->parent_id=active_process_identifier;
    slot->kernel_stack_top=stack_top;
    slot->id=next_process_id++;
    slot->state=PROCESS_STATE_CREATED;
    slot->heap_start=0x0000000010000000ULL;
    slot->heap_break=0x0000000010000000ULL;
    slot->cwd[0]='/';
    slot->cwd[1]='\0';
    for (size_t f=0U;f<DAWN_PROCESS_MAX_FDS;f++) slot->fds[f]=(struct dawn_fd_entry){0};
    if (parent!=NULL) {
        for (size_t f=0U;f<DAWN_PROCESS_MAX_FDS;f++) slot->fds[f]=parent->fds[f];
    } else {
        slot->fds[0]=(struct dawn_fd_entry){ .type=DAWN_FD_TYPE_SERIAL, .flags=DAWN_O_RDONLY };
        slot->fds[1]=(struct dawn_fd_entry){ .type=DAWN_FD_TYPE_SERIAL, .flags=DAWN_O_WRONLY };
        slot->fds[2]=(struct dawn_fd_entry){ .type=DAWN_FD_TYPE_SERIAL, .flags=DAWN_O_WRONLY };
    }
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

bool process_set_exit_status(uint64_t process_id,int64_t exit_status) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_READY ||
        active_process_identifier!=process_id) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->exit_status=exit_status;
    restore_interrupt_flag(flags);
    return true;
}

bool process_mark_exited(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || active_process_identifier==process_id ||
        (record->state!=PROCESS_STATE_CREATED &&
         record->state!=PROCESS_STATE_READY)) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->state=PROCESS_STATE_EXITED;
    restore_interrupt_flag(flags);
    return true;
}

static bool process_switch_to_locked(uint64_t target_id) {
    struct process_record *current=NULL;
    struct process_record *target=NULL;
    uint64_t current_stack=bootstrap_kernel_stack_top;
    if (target_id==active_process_identifier) {
        if (target_id==0ULL)
            return vm_address_space_activate_kernel() &&
                   gdt_tss_activate_kernel_stack(bootstrap_kernel_stack_top);
        target=find_process(target_id);
        return target!=NULL && target->state==PROCESS_STATE_READY &&
               vm_address_space_is_active(&target->address_space) &&
               gdt_tss_kernel_stack_top()==target->kernel_stack_top;
    }
    current=find_process(active_process_identifier);
    if (active_process_identifier!=0ULL) {
        if (current==NULL || current->state!=PROCESS_STATE_READY)
            return false;
        current_stack=current->kernel_stack_top;
    }
    if (target_id==0ULL) {
        if (!vm_address_space_activate_kernel()) return false;
        if (!gdt_tss_activate_kernel_stack(bootstrap_kernel_stack_top)) {
            if (current!=NULL)
                (void)vm_address_space_activate(&current->address_space);
            (void)gdt_tss_activate_kernel_stack(current_stack);
            return false;
        }
        active_process_identifier=0ULL;
        dawn_syscall_kernel_stack_top=bootstrap_kernel_stack_top;
        return true;
    }
    target=find_process(target_id);
    if (target==NULL || target->state!=PROCESS_STATE_READY ||
        target->kernel_stack_top==0ULL) return false;
    {
        uint64_t current_rsp=0ULL;
        uint64_t mapped_physical=0ULL;
        uint32_t current_stack_flags=0U;
        uint32_t process_stack_flags=0U;
        uint32_t code_flags=0U;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(current_rsp));
        if (current_rsp==0ULL ||
            !vm_address_space_translate(&target->address_space,current_rsp,
                &mapped_physical,&current_stack_flags) ||
            (current_stack_flags&(uint32_t)VM_PAGE_USER)!=0U ||
            (current_stack_flags&(uint32_t)VM_PAGE_WRITE)==0U ||
            !vm_address_space_translate(&target->address_space,
                target->kernel_stack_top-1ULL,&mapped_physical,
                &process_stack_flags) ||
            (process_stack_flags&(uint32_t)VM_PAGE_USER)!=0U ||
            (process_stack_flags&(uint32_t)VM_PAGE_WRITE)==0U ||
            !vm_address_space_translate(&target->address_space,
                (uint64_t)(uintptr_t)&process_switch_to_locked,
                &mapped_physical,&code_flags) ||
            (code_flags&(uint32_t)VM_PAGE_USER)!=0U ||
            (code_flags&(uint32_t)VM_PAGE_EXECUTE)==0U)
            return false;
    }
    if (!gdt_tss_activate_kernel_stack(target->kernel_stack_top))
        return false;
    if (!vm_address_space_activate(&target->address_space)) {
        (void)gdt_tss_activate_kernel_stack(current_stack);
        if (current!=NULL)
            (void)vm_address_space_activate(&current->address_space);
        else
            (void)vm_address_space_activate_kernel();
        return false;
    }
    active_process_identifier=target_id;
    dawn_syscall_kernel_stack_top=target->kernel_stack_top;
    return true;
}

bool process_switch_to(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    bool result=false;
    if (!initialized || (read_cs()&3U)!=0U ||
        (flags&CPU_RFLAGS_INTERRUPT_ENABLE)!=0ULL) return false;
    disable_interrupts();
    result=process_switch_to_locked(process_id);
    restore_interrupt_flag(flags);
    return result;
}

bool process_activate(uint64_t process_id) {
    return process_switch_to(process_id);
}

bool process_deactivate(void) {
    return process_switch_to(0ULL);
}

uint64_t process_active_id(void) {
    const uint64_t flags=read_rflags();
    uint64_t id=0ULL;
    if (!initialized || (read_cs()&3U)!=0U) return 0ULL;
    disable_interrupts();
    id=active_process_identifier;
    restore_interrupt_flag(flags);
    return id;
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
    info->parent_id=record->parent_id;
    info->state=record->state;
    info->address_space_root=record->address_space.root_physical;
    info->kernel_stack_top=record->kernel_stack_top;
    info->exit_status=record->exit_status;
    restore_interrupt_flag(flags);
    return true;
}

bool process_get_child_info(uint64_t parent_id,uint64_t child_id,
                            struct process_info *info) {
    const uint64_t flags=read_rflags();
    struct process_record *parent=NULL;
    struct process_record *child=NULL;
    if (!initialized || parent_id==0ULL || child_id==0ULL || info==NULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    parent=find_process(parent_id);
    child=find_process(child_id);
    if (parent==NULL || child==NULL || child->parent_id!=parent_id) {
        restore_interrupt_flag(flags);
        return false;
    }
    info->id=child->id;
    info->parent_id=child->parent_id;
    info->state=child->state;
    info->address_space_root=child->address_space.root_physical;
    info->kernel_stack_top=child->kernel_stack_top;
    info->exit_status=child->exit_status;
    restore_interrupt_flag(flags);
    return true;
}

bool process_has_children(uint64_t parent_id,int64_t target_child_id,
                          uint64_t *out_exited_child_id) {
    const uint64_t flags=read_rflags();
    bool has_child=false;
    if (!initialized || parent_id==0ULL || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    if (out_exited_child_id!=NULL) *out_exited_child_id=0ULL;
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++) {
        if (processes[i].state!=PROCESS_STATE_FREE && processes[i].parent_id==parent_id) {
            if (target_child_id<=0 || processes[i].id==(uint64_t)target_child_id) {
                has_child=true;
                if (processes[i].state==PROCESS_STATE_EXITED) {
                    if (out_exited_child_id!=NULL) {
                        *out_exited_child_id=processes[i].id;
                        break;
                    }
                }
            }
        }
    }
    restore_interrupt_flag(flags);
    return has_child;
}

bool process_collect_child(uint64_t parent_id,uint64_t child_id,
                           uint64_t status_address) {
    const uint64_t flags=read_rflags();
    struct process_record *parent=NULL;
    struct process_record *child=NULL;
    int64_t exit_status=0;
    if (!initialized || parent_id==0ULL || child_id==0ULL ||
        parent_id==child_id || (read_cs()&3U)!=0U ||
        (status_address!=0ULL && (status_address&7ULL)!=0ULL))
        return false;
    disable_interrupts();
    parent=find_process(parent_id);
    child=find_process(child_id);
    if (parent==NULL || child==NULL || parent->state!=PROCESS_STATE_READY ||
        child->parent_id!=parent_id || child->state!=PROCESS_STATE_EXITED ||
        active_process_identifier==child_id) {
        restore_interrupt_flag(flags);
        return false;
    }
    exit_status=child->exit_status;
    if (status_address!=0ULL) {
        uint64_t first_physical=0ULL,last_physical=0ULL;
        uint32_t first_flags=0U,last_flags=0U;
        if (!vm_address_space_translate(&parent->address_space,status_address,
                &first_physical,&first_flags) ||
            (first_flags&((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE))!=
                ((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE) ||
            !vm_address_space_translate(&parent->address_space,
                status_address+sizeof(exit_status)-1ULL,
                &last_physical,&last_flags) ||
            (last_flags&((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE))!=
                ((uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE)) {
            restore_interrupt_flag(flags);
            return false;
        }
    }
    /* Reap before publishing status: a child with unreaped descendants
     * cannot be collected by this bounded process manager. */
    if (!process_reap(child_id)) {
        restore_interrupt_flag(flags);
        return false;
    }
    if (status_address!=0ULL &&
        !vm_address_space_write(&parent->address_space,status_address,
                                &exit_status,sizeof(exit_status))) {
        restore_interrupt_flag(flags);
        return false;
    }
    restore_interrupt_flag(flags);
    return true;
}

bool process_user_page_checkpoint(uint64_t process_id,size_t *checkpoint) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || checkpoint==NULL || (read_cs()&3U)!=0U)
        return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_CREATED) {
        restore_interrupt_flag(flags);
        return false;
    }
    *checkpoint=record->user_page_count;
    restore_interrupt_flag(flags);
    return true;
}

bool process_allocate_user_page(uint64_t process_id,
                                uint64_t virtual_address,uint32_t flags) {
    const uint64_t saved_flags=read_rflags();
    struct process_record *record=NULL;
    uint64_t physical=0ULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_CREATED ||
        record->user_page_count>=DAWN_PROCESS_MAX_USER_PAGES ||
        (virtual_address&4095ULL)!=0ULL || virtual_address==0ULL ||
        (flags&(uint32_t)VM_PAGE_USER)==0U ||
        (flags&((uint32_t)VM_PAGE_WRITE|(uint32_t)VM_PAGE_EXECUTE))==
            ((uint32_t)VM_PAGE_WRITE|(uint32_t)VM_PAGE_EXECUTE) ||
        !physical_allocator_allocate(&physical)) {
        restore_interrupt_flag(saved_flags);
        return false;
    }
    if (!vm_address_space_map_user(&record->address_space,
            virtual_address,physical,flags)) {
        (void)physical_allocator_free(physical);
        restore_interrupt_flag(saved_flags);
        return false;
    }
    record->user_pages[record->user_page_count].virtual_address=
        virtual_address;
    record->user_pages[record->user_page_count].physical_address=physical;
    ++record->user_page_count;
    restore_interrupt_flag(saved_flags);
    return true;
}

bool process_user_memory_write(uint64_t process_id,uint64_t destination,
                               const void *source,size_t length) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    bool result=false;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record!=NULL &&
        (record->state==PROCESS_STATE_CREATED ||
         record->state==PROCESS_STATE_READY))
        result=vm_address_space_write(&record->address_space,destination,
                                      source,length);
    restore_interrupt_flag(flags);
    return result;
}

bool process_user_memory_zero(uint64_t process_id,uint64_t destination,
                              size_t length) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    bool result=false;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record!=NULL && record->state==PROCESS_STATE_CREATED)
        result=vm_address_space_zero(&record->address_space,destination,length);
    restore_interrupt_flag(flags);
    return result;
}

bool process_user_memory_rollback(uint64_t process_id,size_t checkpoint) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    bool result=true;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_CREATED ||
        checkpoint>record->user_page_count) {
        restore_interrupt_flag(flags);
        return false;
    }
    while (record->user_page_count>checkpoint) {
        const size_t index=record->user_page_count-1U;
        const struct process_user_page page=record->user_pages[index];
        if (!physical_allocator_is_allocated(page.physical_address) ||
            !vm_address_space_unmap_user(&record->address_space,
                                         page.virtual_address) ||
            !physical_allocator_free(page.physical_address)) {
            result=false;
            break;
        }
        record->user_pages[index]=(struct process_user_page){0};
        --record->user_page_count;
    }
    result=result && record->user_page_count==checkpoint;
    restore_interrupt_flag(flags);
    return result;
}

bool process_reset_for_exec(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_READY) {
        restore_interrupt_flag(flags);
        return false;
    }
    while (record->user_page_count > 0) {
        const size_t index=record->user_page_count-1U;
        const struct process_user_page page=record->user_pages[index];
        if (!physical_allocator_is_allocated(page.physical_address) ||
            !vm_address_space_unmap_user(&record->address_space,page.virtual_address) ||
            !physical_allocator_free(page.physical_address)) {
            restore_interrupt_flag(flags);
            return false;
        }
        record->user_pages[index]=(struct process_user_page){0};
        --record->user_page_count;
    }
    record->state = PROCESS_STATE_CREATED;
    restore_interrupt_flag(flags);
    return true;
}

bool process_reap(uint64_t process_id) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->state!=PROCESS_STATE_EXITED ||
        active_process_identifier==process_id) {
        restore_interrupt_flag(flags);
        return false;
    }
    /* Keep the parent record alive until every child has itself been reaped. */
    for (size_t child_index=0U;child_index<DAWN_PROCESS_MAX;child_index++) {
        if (processes[child_index].state!=PROCESS_STATE_FREE &&
            processes[child_index].parent_id==process_id) {
            restore_interrupt_flag(flags);
            return false;
        }
    }
    if (scheduler_is_initialized()) {
        (void)scheduler_discard_process_threads(process_id);
    }
    while (record->user_page_count>0U) {
        const size_t index=record->user_page_count-1U;
        const struct process_user_page page=record->user_pages[index];
        if (!physical_allocator_is_allocated(page.physical_address) ||
            !vm_address_space_unmap_user(&record->address_space,
                                         page.virtual_address) ||
            !physical_allocator_free(page.physical_address)) {
            restore_interrupt_flag(flags);
            return false;
        }
        record->user_pages[index]=(struct process_user_page){0};
        --record->user_page_count;
    }
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

bool process_fd_get(uint64_t process_id,int fd,struct dawn_fd_entry *entry) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || fd<0 || fd>=(int)DAWN_PROCESS_MAX_FDS || entry==NULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->fds[fd].type==DAWN_FD_TYPE_UNUSED) {
        restore_interrupt_flag(flags);
        return false;
    }
    *entry=record->fds[fd];
    restore_interrupt_flag(flags);
    return true;
}

bool process_fd_set(uint64_t process_id,int fd,const struct dawn_fd_entry *entry) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || fd<0 || fd>=(int)DAWN_PROCESS_MAX_FDS || entry==NULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->fds[fd]=*entry;
    restore_interrupt_flag(flags);
    return true;
}

bool process_fd_allocate(uint64_t process_id,enum dawn_fd_type type,uint32_t flags,
                         void *node,int *out_fd) {
    const uint64_t saved_flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || type==DAWN_FD_TYPE_UNUSED || out_fd==NULL ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL) {
        restore_interrupt_flag(saved_flags);
        return false;
    }
    for (int fd=0;fd<(int)DAWN_PROCESS_MAX_FDS;fd++) {
        if (record->fds[fd].type==DAWN_FD_TYPE_UNUSED) {
            record->fds[fd].type=type;
            record->fds[fd].flags=flags;
            record->fds[fd].offset=0ULL;
            record->fds[fd].node=node;
            *out_fd=fd;
            restore_interrupt_flag(saved_flags);
            return true;
        }
    }
    restore_interrupt_flag(saved_flags);
    return false;
}

bool process_fd_close(uint64_t process_id,int fd) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || fd<0 || fd>=(int)DAWN_PROCESS_MAX_FDS ||
        (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->fds[fd].type==DAWN_FD_TYPE_UNUSED) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->fds[fd]=(struct dawn_fd_entry){0};
    restore_interrupt_flag(flags);
    return true;
}

bool process_fd_dup(uint64_t process_id,int oldfd,int *out_newfd) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || oldfd<0 || oldfd>=(int)DAWN_PROCESS_MAX_FDS ||
        out_newfd==NULL || (read_cs()&3U)!=0U) return false;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->fds[oldfd].type==DAWN_FD_TYPE_UNUSED) {
        restore_interrupt_flag(flags);
        return false;
    }
    for (int fd=0;fd<(int)DAWN_PROCESS_MAX_FDS;fd++) {
        if (record->fds[fd].type==DAWN_FD_TYPE_UNUSED) {
            record->fds[fd]=record->fds[oldfd];
            *out_newfd=fd;
            restore_interrupt_flag(flags);
            return true;
        }
    }
    restore_interrupt_flag(flags);
    return false;
}

bool process_fd_dup2(uint64_t process_id,int oldfd,int newfd) {
    const uint64_t flags=read_rflags();
    struct process_record *record=NULL;
    if (!initialized || oldfd<0 || oldfd>=(int)DAWN_PROCESS_MAX_FDS ||
        newfd<0 || newfd>=(int)DAWN_PROCESS_MAX_FDS || (read_cs()&3U)!=0U)
        return false;
    if (oldfd==newfd) return true;
    disable_interrupts();
    record=find_process(process_id);
    if (record==NULL || record->fds[oldfd].type==DAWN_FD_TYPE_UNUSED) {
        restore_interrupt_flag(flags);
        return false;
    }
    record->fds[newfd]=record->fds[oldfd];
    restore_interrupt_flag(flags);
    return true;
}

uint64_t process_brk(uint64_t process_id, uint64_t new_brk) {
    const uint64_t flags = read_rflags();
    struct process_record *record = NULL;
    uint64_t result = 0ULL;
    if (!initialized || process_id == 0ULL || (read_cs() & 3U) != 0U) return 0ULL;
    disable_interrupts();
    record = find_process(process_id);
    if (record == NULL) {
        restore_interrupt_flag(flags);
        return 0ULL;
    }
    if (new_brk == 0ULL || new_brk == record->heap_break) {
        result = record->heap_break;
        restore_interrupt_flag(flags);
        return result;
    }
    if (new_brk < record->heap_start) {
        result = record->heap_break;
        restore_interrupt_flag(flags);
        return result;
    }
    if (new_brk > record->heap_break) {
        uint64_t old_page_aligned = (record->heap_break + 4095ULL) & ~4095ULL;
        uint64_t new_page_aligned = (new_brk + 4095ULL) & ~4095ULL;
        for (uint64_t va = old_page_aligned; va < new_page_aligned; va += 4096ULL) {
            if (record->user_page_count >= DAWN_PROCESS_MAX_USER_PAGES) {
                result = record->heap_break;
                restore_interrupt_flag(flags);
                return result;
            }
            uint64_t phys = 0ULL;
            if (!physical_allocator_allocate(&phys)) {
                result = record->heap_break;
                restore_interrupt_flag(flags);
                return result;
            }
            if (!vm_address_space_map_user(&record->address_space, va, phys,
                                          (uint32_t)VM_PAGE_USER | (uint32_t)VM_PAGE_WRITE)) {
                (void)physical_allocator_free(phys);
                result = record->heap_break;
                restore_interrupt_flag(flags);
                return result;
            }
            record->user_pages[record->user_page_count].virtual_address = va;
            record->user_pages[record->user_page_count].physical_address = phys;
            record->user_page_count++;
        }
        record->heap_break = new_brk;
        result = record->heap_break;
    } else {
        record->heap_break = new_brk;
        result = record->heap_break;
    }
    restore_interrupt_flag(flags);
    return result;
}

bool process_getcwd(uint64_t process_id, char *out_buf, size_t buf_size) {
    const uint64_t flags = read_rflags();
    struct process_record *record = NULL;
    if (!initialized || process_id == 0ULL || out_buf == NULL || buf_size == 0U ||
        (read_cs() & 3U) != 0U) return false;
    disable_interrupts();
    record = find_process(process_id);
    if (record == NULL) {
        restore_interrupt_flag(flags);
        return false;
    }
    size_t i = 0;
    while (record->cwd[i] != '\0' && i + 1 < buf_size) {
        out_buf[i] = record->cwd[i];
        i++;
    }
    out_buf[i] = '\0';
    restore_interrupt_flag(flags);
    return true;
}

bool process_chdir(uint64_t process_id, const char *path) {
    const uint64_t flags = read_rflags();
    struct process_record *record = NULL;
    if (!initialized || process_id == 0ULL || path == NULL || (read_cs() & 3U) != 0U)
        return false;
    disable_interrupts();
    record = find_process(process_id);
    if (record == NULL) {
        restore_interrupt_flag(flags);
        return false;
    }
    size_t i = 0;
    while (path[i] != '\0' && i + 1 < sizeof(record->cwd)) {
        record->cwd[i] = path[i];
        i++;
    }
    record->cwd[i] = '\0';
    restore_interrupt_flag(flags);
    return true;
}

bool process_spawn(const void *image,size_t image_size,
                   int argc,const char *const argv[],const char *const envp[],
                   uint64_t *out_pid) {
    uint64_t child_pid=0ULL;
    uint64_t thread_id=0ULL;
    struct elf64_loaded_process loaded={0};
    uint64_t rdi=0ULL,rsi=0ULL,rdx=0ULL;

    if (image==NULL || image_size==0U || out_pid==NULL || (read_cs()&3U)!=0U)
        return false;

    if (!process_create(&child_pid)) return false;

    if (!elf64_load_process(child_pid,image,image_size,&loaded)) {
        (void)process_mark_exited(child_pid);
        (void)process_reap(child_pid);
        return false;
    }

    if ((argc>0 && argv!=NULL) || envp!=NULL) {
        if (!elf64_setup_user_stack(child_pid,&loaded.user_stack_top,
                                    argc,argv,envp,&rdi,&rsi,&rdx)) {
            (void)process_mark_exited(child_pid);
            (void)process_reap(child_pid);
            return false;
        }
    }

    if (!process_mark_ready(child_pid)) {
        (void)process_mark_exited(child_pid);
        (void)process_reap(child_pid);
        return false;
    }

    if (!kernel_thread_create_user(&thread_id)) {
        (void)process_mark_exited(child_pid);
        (void)process_reap(child_pid);
        return false;
    }

    if (!kernel_thread_configure_user_args(thread_id,child_pid,
                                          loaded.entry,loaded.user_stack_top,
                                          rdi,rsi,rdx)) {
        (void)process_mark_exited(child_pid);
        (void)process_reap(child_pid);
        return false;
    }

    *out_pid=child_pid;
    return true;
}

#ifdef DAWN_KERNEL_TEST
#define PROCESS_TEST_COM1 0x3f8U

static inline void process_test_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t process_test_inb(uint16_t port) {
    uint8_t value = 0U;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void process_test_log(const char *message) {
    if (message == NULL) return;
    while (*message != '\0') {
        uint32_t spins = 0U;
        while ((process_test_inb(PROCESS_TEST_COM1 + 5U) & 0x20U) == 0U) {
            if (++spins > 1000000U) return;
        }
        if (*message == '\n') {
            process_test_outb(PROCESS_TEST_COM1, (uint8_t)'\r');
            spins = 0U;
            while ((process_test_inb(PROCESS_TEST_COM1 + 5U) & 0x20U) == 0U) {
                if (++spins > 1000000U) return;
            }
        }
        process_test_outb(PROCESS_TEST_COM1, (uint8_t)*message);
        ++message;
    }
}

static bool process_manager_self_test_locked(void) {
    uint64_t ids[DAWN_PROCESS_MAX]={0ULL};
    struct process_info info={0};
    uint64_t extra=0ULL;
    if (!initialized) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: not initialized\n");
        return false;
    }
    if (process_create(NULL)) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_create(NULL) did not fail\n");
        return false;
    }
    if (process_mark_ready(0ULL) || process_mark_exited(0ULL)) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: PID 0 state transitions did not fail\n");
        return false;
    }
    for (size_t i=0U;i<DAWN_PROCESS_MAX;i++) {
        if (!process_create(&ids[i]) || ids[i]==0ULL) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_create failed during capacity fill\n");
            return false;
        }
        if (!process_get_info(ids[i],&info) ||
            info.id!=ids[i] || info.state!=PROCESS_STATE_CREATED ||
            info.address_space_root==0ULL || info.kernel_stack_top==0ULL) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: invalid process_info for new process\n");
            return false;
        }
        for (size_t j=0U;j<i;j++) {
            if (ids[i]==ids[j]) {
                process_test_log("DAWN-TEST: FAIL: process resource lifecycle: duplicate PID allocated\n");
                return false;
            }
        }
    }
    if (process_create(&extra) || extra!=0ULL) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process table overflow allowed\n");
        return false;
    }
    if (!process_mark_ready(ids[0]) || process_mark_ready(ids[0]) || process_reap(ids[0])) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: ready state transition or un-exited reap validation failed\n");
        return false;
    }
    if (!process_activate(ids[0]) || process_active_id()!=ids[0] ||
        !process_get_info(ids[0],&info) ||
        !vm_address_space_is_active(&processes[0].address_space) ||
        gdt_tss_kernel_stack_top()!=info.kernel_stack_top) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_activate or TSS stack check failed\n");
        return false;
    }
    if (!process_mark_ready(ids[1]) || !process_switch_to(ids[1]) ||
        process_active_id()!=ids[1] ||
        !vm_address_space_is_active(&processes[1].address_space) ||
        gdt_tss_kernel_stack_top()!=processes[1].kernel_stack_top) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_switch_to second process failed\n");
        return false;
    }
    if (!process_switch_to(ids[0]) || process_active_id()!=ids[0]) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_switch_to first process failed\n");
        return false;
    }
    if (process_mark_exited(ids[0]) || process_reap(ids[0])) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: active process allowed to exit or reap\n");
        return false;
    }
    if (!process_deactivate() || process_active_id()!=0ULL ||
        vm_address_space_is_active(&processes[0].address_space) ||
        gdt_tss_kernel_stack_top()!=bootstrap_kernel_stack_top) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: process_deactivate failed\n");
        return false;
    }
    if (!process_mark_exited(ids[0]) || process_mark_exited(ids[0]) ||
        !process_reap(ids[0]) || process_get_info(ids[0],&info)) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: exited state or reap failed for ids[0]\n");
        return false;
    }
    for (size_t i=1U;i<DAWN_PROCESS_MAX;i++) {
        if (!process_mark_exited(ids[i]) || !process_reap(ids[i]) ||
            process_get_info(ids[i],&info)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: cleanup reap failed for table\n");
            return false;
        }
    }
    if (!process_create(&extra) || extra<=ids[DAWN_PROCESS_MAX-1U] ||
        !process_mark_exited(extra) || !process_reap(extra)) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: slot reuse after full table reap failed\n");
        return false;
    }

    /* A child inherits the active process as its parent. Parent resources
     * remain allocated until all child records have been reaped. */
    {
        uint64_t parent_id=0ULL,child_id=0ULL,exited_found=0ULL;
        struct process_info child_info={0};
        if (!process_create(&parent_id) || !process_mark_ready(parent_id) ||
            process_has_children(parent_id,0,&exited_found) ||
            !process_activate(parent_id) || process_active_id()!=parent_id ||
            !process_create(&child_id) || child_id==0ULL ||
            !process_has_children(parent_id,0,&exited_found) || exited_found!=0ULL ||
            !process_has_children(parent_id,(int64_t)child_id,&exited_found) ||
            process_has_children(parent_id,99999,&exited_found) ||
            !process_get_child_info(parent_id,child_id,&child_info) ||
            child_info.parent_id!=parent_id ||
            child_info.state!=PROCESS_STATE_CREATED ||
            !process_mark_ready(child_id) || !process_deactivate() ||
            !process_mark_exited(child_id) ||
            !process_has_children(parent_id,0,&exited_found) || exited_found!=child_id ||
            !process_mark_exited(parent_id) ||
            process_reap(parent_id) ||
            !process_get_child_info(parent_id,child_id,&child_info) ||
            child_info.state!=PROCESS_STATE_EXITED ||
            !process_reap(child_id) ||
            process_has_children(parent_id,0,&exited_found) ||
            !process_reap(parent_id) ||
            process_get_child_info(parent_id,child_id,&child_info)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: parent-child hierarchy and cascade reap protection failed\n");
            return false;
        }
    }

    /* File descriptor table validation */
    {
        uint64_t pid=0ULL;
        struct dawn_fd_entry fd_entry={0};
        int new_fd=-1,dup_fd=-1;
        if (!process_create(&pid) || !process_mark_ready(pid)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD test process creation failed\n");
            return false;
        }
        if (!process_fd_get(pid,0,&fd_entry) || fd_entry.type!=DAWN_FD_TYPE_SERIAL ||
            (fd_entry.flags&DAWN_O_RDONLY)==0U) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: stdin FD 0 not configured\n");
            return false;
        }
        if (!process_fd_get(pid,1,&fd_entry) || fd_entry.type!=DAWN_FD_TYPE_SERIAL ||
            (fd_entry.flags&DAWN_O_WRONLY)==0U) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: stdout FD 1 not configured\n");
            return false;
        }
        if (!process_fd_get(pid,2,&fd_entry) || fd_entry.type!=DAWN_FD_TYPE_SERIAL ||
            (fd_entry.flags&DAWN_O_WRONLY)==0U) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: stderr FD 2 not configured\n");
            return false;
        }
        if (process_fd_get(pid,3,&fd_entry)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: unallocated FD 3 exists\n");
            return false;
        }
        if (!process_fd_allocate(pid,DAWN_FD_TYPE_NULL,DAWN_O_RDWR,NULL,&new_fd) || new_fd!=3) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD allocation failed to return FD 3\n");
            return false;
        }
        if (!process_fd_dup(pid,new_fd,&dup_fd) || dup_fd!=4) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD dup failed to return FD 4\n");
            return false;
        }
        if (!process_fd_dup2(pid,new_fd,10)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD dup2 to FD 10 failed\n");
            return false;
        }
        if (!process_fd_get(pid,10,&fd_entry) || fd_entry.type!=DAWN_FD_TYPE_NULL) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD 10 entry invalid after dup2\n");
            return false;
        }
        if (!process_fd_close(pid,new_fd) || process_fd_get(pid,new_fd,&fd_entry)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD close failed\n");
            return false;
        }
        if (!process_mark_exited(pid) || !process_reap(pid)) {
            process_test_log("DAWN-TEST: FAIL: process resource lifecycle: FD test process cleanup failed\n");
            return false;
        }
    }
    return true;
}

bool process_manager_self_test(void) {
    const uint64_t flags=read_rflags();
    bool result=false;
    if (!initialized || (read_cs()&3U)!=0U) {
        process_test_log("DAWN-TEST: FAIL: process resource lifecycle: manager uninitialized or ring 3\n");
        return false;
    }
    disable_interrupts();
    result=process_manager_self_test_locked();
    restore_interrupt_flag(flags);
    return result;
}

bool process_spawn_self_test(void) {
    const uint64_t flags=read_rflags();
    bool result=false;
    if (!initialized || !scheduler_is_initialized() || (read_cs()&3U)!=0U) {
        process_test_log("DAWN-TEST: FAIL: process spawn self-test: scheduler uninitialized or ring 3\n");
        return false;
    }
    disable_interrupts();
    {
        uint8_t image[8192]={0U};
        size_t image_size=0U;
        uint64_t spawned_pid=0ULL;
        const char *test_args[]={"spawned",NULL};
        if (!elf64_build_test_exit_image(image,sizeof(image),&image_size)) {
            process_test_log("DAWN-TEST: FAIL: process spawn self-test: elf64_build_test_exit_image failed\n");
        } else if (!process_spawn(image,image_size,1,test_args,NULL,&spawned_pid) || spawned_pid==0ULL) {
            process_test_log("DAWN-TEST: FAIL: process spawn self-test: process_spawn failed\n");
        } else if (!process_mark_exited(spawned_pid) || !process_reap(spawned_pid)) {
            process_test_log("DAWN-TEST: FAIL: process spawn self-test: cleanup reap failed\n");
        } else {
            result=true;
        }
    }
    restore_interrupt_flag(flags);
    return result;
}
#endif
