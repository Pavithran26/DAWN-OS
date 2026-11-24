/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_TASK_MANAGER_H
#define DAWN_TASK_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "window_manager.h"
#include "process.h"
#include "scheduler.h"

#define TASKMGR_MAX_RECORDS 32U
#define TASKMGR_HISTORY_LEN 32U
#define TASKMGR_WIDTH       520U
#define TASKMGR_HEIGHT      340U

#define TASKMGR_MIN_REFRESH_INTERVAL_TICKS 5ULL

#define TASKMGR_SUCCESS            0
#define TASKMGR_ERR_PROTECTED     -1
#define TASKMGR_ERR_NOT_FOUND     -2
#define TASKMGR_ERR_ALREADY_DEAD  -3
#define TASKMGR_ERR_INVALID_PID   -4

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
    int window_id;
    uint32_t render_buffer[TASKMGR_WIDTH * TASKMGR_HEIGHT];
    bool active;

    /* Bounded refresh rate limiting */
    uint64_t last_refresh_tick;
    uint64_t refresh_count;
    uint64_t refresh_rate_limited_count;

    /* Monitoring overhead metrics */
    uint64_t last_refresh_cycles;
    uint64_t last_render_cycles;
    uint64_t total_overhead_cycles;

    /* Scheduler telemetry snapshot */
    struct scheduler_stats last_sched_stats;
};

bool task_manager_initialize(void);
struct task_manager *task_manager_get_instance(void);
void task_manager_refresh(struct task_manager *tm);
void task_manager_refresh_forced(struct task_manager *tm);
int task_manager_kill_process(struct task_manager *tm, uint64_t pid);
int task_manager_kill_selected(struct task_manager *tm);
void task_manager_render(struct task_manager *tm);
void task_manager_handle_click(struct task_manager *tm, int32_t rel_x, int32_t rel_y);

/* Desktop & Window Manager Lifecycle */
bool task_manager_window_open(void);
void task_manager_window_close(void);
int task_manager_window_get_id(void);

/* Monitoring Overhead Telemetry Query */
void task_manager_get_overhead(uint64_t *out_refresh_cycles, uint64_t *out_render_cycles);

bool task_manager_self_test(void);

#endif /* DAWN_TASK_MANAGER_H */
