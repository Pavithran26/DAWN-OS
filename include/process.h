/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PROCESS_H
#define DAWN_PROCESS_H
#include <stdbool.h>
#include <stdint.h>

#define DAWN_PROCESS_MAX 4U

enum process_state {
    PROCESS_STATE_FREE = 0,
    PROCESS_STATE_CREATED,
    PROCESS_STATE_READY,
    PROCESS_STATE_EXITED
};

struct process_info {
    uint64_t id;
    enum process_state state;
    uint64_t address_space_root;
    uint64_t kernel_stack_top;
};

/* Metadata/resource lifecycle only: this manager does not schedule user tasks. */
bool process_manager_initialize(void);
bool process_create(uint64_t *process_id);
bool process_mark_ready(uint64_t process_id);
bool process_mark_exited(uint64_t process_id);
bool process_get_info(uint64_t process_id, struct process_info *info);
bool process_reap(uint64_t process_id);

#ifdef DAWN_KERNEL_TEST
bool process_manager_self_test(void);
#endif
#endif
