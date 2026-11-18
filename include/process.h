/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PROCESS_H
#define DAWN_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_PROCESS_MAX 4U
#define DAWN_PROCESS_MAX_USER_PAGES 256U

enum process_state {
    PROCESS_STATE_FREE = 0,
    PROCESS_STATE_CREATED,
    PROCESS_STATE_READY,
    PROCESS_STATE_EXITED
};

struct process_info {
    uint64_t id;
    uint64_t parent_id;
    enum process_state state;
    uint64_t address_space_root;
    uint64_t kernel_stack_top;
    int64_t exit_status;
};

/* Process metadata, isolated mappings, user-frame ownership, and lifecycle. */
bool process_manager_initialize(void);
bool process_create(uint64_t *process_id);
/* Return metadata only when child_id belongs to parent_id. */
bool process_get_child_info(uint64_t parent_id,uint64_t child_id,
                            struct process_info *info);
bool process_mark_ready(uint64_t process_id);
bool process_mark_exited(uint64_t process_id);
/* Record the status supplied by the active process's SYS_EXIT call. */
bool process_set_exit_status(uint64_t process_id,int64_t exit_status);
/* Activation switches CR3 and TSS.RSP0; caller must be CPL0 with IF clear. */
bool process_activate(uint64_t process_id);
bool process_switch_to(uint64_t process_id);
bool process_deactivate(void);
uint64_t process_active_id(void);
bool process_get_info(uint64_t process_id, struct process_info *info);
/* ELF image builder APIs: only CREATED processes may acquire additional user pages. */
bool process_user_page_checkpoint(uint64_t process_id,size_t *checkpoint);
bool process_allocate_user_page(uint64_t process_id,uint64_t virtual_address,uint32_t flags);
bool process_user_memory_write(uint64_t process_id,uint64_t destination,const void *source,size_t length);
bool process_user_memory_zero(uint64_t process_id,uint64_t destination,size_t length);
bool process_user_memory_rollback(uint64_t process_id,size_t checkpoint);
bool process_reap(uint64_t process_id);

#ifdef DAWN_KERNEL_TEST
bool process_manager_self_test(void);
#endif
#endif
