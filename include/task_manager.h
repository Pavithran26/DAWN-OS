/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_TASK_MANAGER_H
#define DAWN_TASK_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "window_manager.h"
#include "process.h"

#define TASKMGR_MAX_RECORDS 32U
#define TASKMGR_HISTORY_LEN 32U
#define TASKMGR_WIDTH       520U
#define TASKMGR_HEIGHT      340U

enum taskmgr_tab {
    TASKMGR_TAB_PROCESSES = 0,
    TASKMGR_TAB_PERFORMANCE = 1
};

struct taskmgr_proc_item {
    uint64_t pid;
    uint64_t ppid;
    char name[32];
    enum process_state state;
    uint32_t cpu_percent;
    uint64_t mem_bytes;
    bool is_user;
};

struct task_manager {
    enum taskmgr_tab current_tab;
    struct taskmgr_proc_item items[TASKMGR_MAX_RECORDS];
    size_t item_count;
    uint64_t selected_pid;
    uint32_t cpu_history[TASKMGR_HISTORY_LEN];
    uint32_t history_idx;
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint32_t current_cpu_usage;
    struct window *win;
    uint32_t render_buffer[TASKMGR_WIDTH * TASKMGR_HEIGHT];
    bool active;
};

bool task_manager_initialize(void);
struct task_manager *task_manager_get_instance(void);
void task_manager_refresh(struct task_manager *tm);
int task_manager_kill_selected(struct task_manager *tm);
void task_manager_render(struct task_manager *tm);
void task_manager_handle_click(struct task_manager *tm, int32_t rel_x, int32_t rel_y);

bool task_manager_self_test(void);

#endif /* DAWN_TASK_MANAGER_H */
