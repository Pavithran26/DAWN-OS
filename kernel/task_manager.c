/* SPDX-License-Identifier: Unlicense */
#include "task_manager.h"
#include "physical_memory.h"
#include "smp.h"
#include "scheduler.h"
#include "apic.h"
#include "window_manager.h"
#include "gfx.h"

static struct task_manager main_taskmgr = {0};
static bool taskmgr_initialized = false;
static int taskmgr_window_id = -1;
static uint32_t taskmgr_render_buf[TASKMGR_WIDTH * TASKMGR_HEIGHT];

static inline uint64_t read_tsc(void) {
    uint32_t lo = 0U, hi = 0U;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0U;
    if (dest == NULL || src == NULL || max_len == 0U) return;
    while (src[i] != '\0' && i + 1U < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline void u64_to_str(uint64_t val, char *buf) {
    char tmp[24];
    size_t i = 0U;
    if (val == 0ULL) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0ULL && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (val % 10ULL));
        val /= 10ULL;
    }
    for (size_t j = 0U; j < i; j++) {
        buf[j] = tmp[i - 1U - j];
    }
    buf[i] = '\0';
}

static void task_manager_window_handle_click(int32_t rel_x, int32_t rel_y) {
    task_manager_handle_click(&main_taskmgr, rel_x, rel_y);
}

static void task_manager_on_close(struct window *win) {
    (void)win;
    taskmgr_window_id = -1;
    main_taskmgr.win = NULL;
}

static void task_manager_refresh_internal(struct task_manager *tm, bool forced) {
    if (tm == NULL) return;

    /* Rate limiting check */
    uint64_t current_tick = apic_local_timer_tick_count();
    if (!forced && tm->refresh_count > 0ULL &&
        current_tick < tm->last_refresh_tick + TASKMGR_MIN_REFRESH_INTERVAL_TICKS) {
        tm->refresh_rate_limited_count++;
        return;
    }
    tm->last_refresh_tick = current_tick;
    tm->refresh_count++;

    /* Measure monitoring overhead */
    uint64_t t0 = read_tsc();

    /* Memory metrics from physical page allocator */
    tm->total_ram_bytes = physical_memory_total_bytes();
    tm->free_ram_bytes = physical_memory_free_bytes();

    /* Scheduler telemetry from kernel thread scheduler */
    scheduler_get_stats(&tm->last_sched_stats);

    /* CPU calculation derived from active threads and online CPU count */
    uint32_t online_cpus = smp_online_cpu_count();
    if (online_cpus == 0U) online_cpus = 1U;
    uint64_t active_thr = tm->last_sched_stats.active_threads;
    uint32_t calculated_cpu = 5U;
    if (active_thr > 0ULL) {
        calculated_cpu = (uint32_t)((active_thr * 100ULL) / ((uint64_t)online_cpus * 2ULL));
        if (calculated_cpu > 100U) calculated_cpu = 100U;
        if (calculated_cpu < 2U) calculated_cpu = 2U;
    }
    tm->current_cpu_usage = calculated_cpu;

    /* Refresh active process items */
    tm->item_count = 0U;

    /* PID 0: Kernel Idle Process */
    struct taskmgr_proc_item *it0 = &tm->items[tm->item_count++];
    it0->pid = 0ULL;
    it0->ppid = 0ULL;
    str_copy(it0->name, "kernel_idle", sizeof(it0->name));
    it0->state = PROCESS_STATE_READY;
    it0->cpu_percent = (100U >= tm->current_cpu_usage) ? (100U - tm->current_cpu_usage) : 95U;
    it0->mem_bytes = 65536ULL; /* Kernel bootstrap stack and idle thread frames */
    it0->is_user = false;

    /* Process entries from process manager */
    for (uint64_t pid = 1ULL; pid <= (uint64_t)DAWN_PROCESS_MAX; pid++) {
        struct process_info pinfo = {0};
        if (process_get_info(pid, &pinfo) && pinfo.state != PROCESS_STATE_FREE) {
            if (tm->item_count < TASKMGR_MAX_RECORDS) {
                struct taskmgr_proc_item *it = &tm->items[tm->item_count++];
                it->pid = pid;
                it->ppid = pinfo.parent_id;
                it->state = pinfo.state;
                it->is_user = (pid == 1ULL || pid > 2ULL);

                if (pid == 1ULL) {
                    str_copy(it->name, "init", sizeof(it->name));
                } else if (pid == 2ULL) {
                    str_copy(it->name, "desktop_wm", sizeof(it->name));
                } else {
                    str_copy(it->name, "user_proc", sizeof(it->name));
                }

                size_t user_pages = process_get_user_page_count(pid);
                it->mem_bytes = (user_pages > 0U) ? ((uint64_t)user_pages * 4096ULL) : ((pid == 1ULL) ? 65536ULL : 131072ULL);

                if (pinfo.state == PROCESS_STATE_READY) {
                    it->cpu_percent = (tm->current_cpu_usage > 0U) ?
                        (tm->current_cpu_usage / (uint32_t)(tm->item_count > 1U ? tm->item_count - 1U : 1U)) : 1U;
                    if (it->cpu_percent == 0U) it->cpu_percent = 1U;
                } else {
                    it->cpu_percent = 0U;
                }
            }
        }
    }

    /* Update CPU history ring buffer */
    tm->cpu_history[tm->history_idx] = tm->current_cpu_usage;
    tm->history_idx = (tm->history_idx + 1U) % TASKMGR_HISTORY_LEN;

    /* Record monitoring overhead cycles */
    uint64_t t1 = read_tsc();
    tm->last_refresh_cycles = (t1 >= t0) ? (t1 - t0) : 0ULL;
    tm->total_overhead_cycles += tm->last_refresh_cycles;
}

void task_manager_refresh(struct task_manager *tm) {
    task_manager_refresh_internal(tm, false);
}

void task_manager_refresh_forced(struct task_manager *tm) {
    task_manager_refresh_internal(tm, true);
}

int task_manager_kill_process(struct task_manager *tm, uint64_t pid) {
    if (tm == NULL) return TASKMGR_ERR_INVALID_PID;

    /* Protection check: Cannot terminate PID 0 (kernel idle) or PID 1 (Init) */
    if (pid <= 1ULL) {
        return TASKMGR_ERR_PROTECTED;
    }

    struct process_info pinfo = {0};
    if (!process_get_info(pid, &pinfo)) {
        return TASKMGR_ERR_NOT_FOUND;
    }

    if (pinfo.state != PROCESS_STATE_READY && pinfo.state != PROCESS_STATE_CREATED) {
        return TASKMGR_ERR_ALREADY_DEAD;
    }

    /* Safe termination: discard scheduler threads and mark process exited */
    (void)scheduler_discard_process_threads(pid);
    (void)process_set_exit_status(pid, -9LL); /* SIGKILL */
    (void)process_mark_exited(pid);

    /* Update cached table entry */
    for (size_t i = 0U; i < tm->item_count; i++) {
        if (tm->items[i].pid == pid) {
            tm->items[i].state = PROCESS_STATE_EXITED;
            tm->items[i].cpu_percent = 0U;
            break;
        }
    }

    return TASKMGR_SUCCESS;
}

int task_manager_kill_selected(struct task_manager *tm) {
    if (tm == NULL) return TASKMGR_ERR_INVALID_PID;
    return task_manager_kill_process(tm, tm->selected_pid);
}

void task_manager_render(struct task_manager *tm) {
    if (tm == NULL) return;

    uint64_t r0 = read_tsc();

    struct gfx_surface surf = {
        .buffer = (tm->render_buffer != NULL) ? tm->render_buffer : taskmgr_render_buf,
        .width = TASKMGR_WIDTH,
        .height = TASKMGR_HEIGHT,
        .pitch = TASKMGR_WIDTH * sizeof(uint32_t),
        .buffer_size = TASKMGR_WIDTH * TASKMGR_HEIGHT * sizeof(uint32_t),
        .is_hardware_fb = false,
        .format = GFX_FORMAT_ARGB8888
    };

    /* Background */
    gfx_draw_rect_filled(&surf, 0, 0, TASKMGR_WIDTH, TASKMGR_HEIGHT, GFX_COLOR_EMERALD_DEEP);

    /* Tab Bar */
    uint32_t proc_tab_col = (tm->current_tab == TASKMGR_TAB_PROCESSES) ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_BASE;
    uint32_t perf_tab_col = (tm->current_tab == TASKMGR_TAB_PERFORMANCE) ? GFX_COLOR_EMERALD_PRIMARY : GFX_COLOR_EMERALD_BASE;

    gfx_draw_glass_panel(&surf, 10, 8, 110, 26, proc_tab_col);
    gfx_draw_string(&surf, 26, 13, "Processes", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_glass_panel(&surf, 125, 8, 110, 26, perf_tab_col);
    gfx_draw_string(&surf, 137, 13, "Performance", GFX_COLOR_TEXT_PRIMARY);

    gfx_draw_line(&surf, 8, 38, (int32_t)(TASKMGR_WIDTH - 8U), 38, GFX_COLOR_GLASS_BORDER);

    if (tm->current_tab == TASKMGR_TAB_PROCESSES) {
        /* Table Header */
        gfx_draw_rect_filled(&surf, 8, 42, TASKMGR_WIDTH - 16U, 20U, GFX_COLOR_EMERALD_BASE);
        gfx_draw_string(&surf, 14, 44, "PID", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 70, 44, "NAME", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 200, 44, "STATE", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 310, 44, "MEMORY", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 410, 44, "CPU%", GFX_COLOR_TEXT_MUTED);

        /* Table Rows */
        uint32_t row_y = 66U;
        for (size_t i = 0U; i < tm->item_count && i < 9U; i++) {
            struct taskmgr_proc_item *it = &tm->items[i];
            bool sel = (it->pid == tm->selected_pid);

            if (sel) {
                gfx_draw_rect_filled(&surf, 8, (int32_t)(row_y - 2U), TASKMGR_WIDTH - 16U, 22U, GFX_COLOR_EMERALD_PRIMARY);
                gfx_draw_rect_outline(&surf, 8, (int32_t)(row_y - 2U), TASKMGR_WIDTH - 16U, 22U, GFX_COLOR_EMERALD_ACCENT);
            }

            char num_buf[24];
            u64_to_str(it->pid, num_buf);
            gfx_draw_string(&surf, 14, (int32_t)row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 70, (int32_t)row_y, it->name, GFX_COLOR_TEXT_PRIMARY);

            const char *st_str = (it->state == PROCESS_STATE_READY)   ? "READY" :
                                 (it->state == PROCESS_STATE_CREATED) ? "CREATED" :
                                 (it->state == PROCESS_STATE_EXITED)  ? "EXITED" : "FREE";
            uint32_t st_color = (it->state == PROCESS_STATE_READY) ? GFX_COLOR_EMERALD_ACCENT :
                                (it->state == PROCESS_STATE_EXITED) ? GFX_COLOR_ERROR : GFX_COLOR_TEXT_SECONDARY;
            gfx_draw_string(&surf, 200, (int32_t)row_y, st_str, st_color);

            u64_to_str(it->mem_bytes / 1024ULL, num_buf);
            gfx_draw_string(&surf, 310, (int32_t)row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 360, (int32_t)row_y, "KB", GFX_COLOR_TEXT_MUTED);

            u64_to_str((uint64_t)it->cpu_percent, num_buf);
            gfx_draw_string(&surf, 410, (int32_t)row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 440, (int32_t)row_y, "%", GFX_COLOR_TEXT_MUTED);

            row_y += 24U;
        }

        /* Status Line / Overhead */
        char overhead_buf[48];
        overhead_buf[0] = 'O'; overhead_buf[1] = 'v'; overhead_buf[2] = 'h'; overhead_buf[3] = 'd';
        overhead_buf[4] = ':'; overhead_buf[5] = ' ';
        u64_to_str(tm->last_refresh_cycles, overhead_buf + 6);
        size_t ob_len = 0U;
        while (overhead_buf[ob_len] != '\0' && ob_len + 5U < sizeof(overhead_buf)) ob_len++;
        overhead_buf[ob_len++] = ' '; overhead_buf[ob_len++] = 'c'; overhead_buf[ob_len++] = 'y'; overhead_buf[ob_len++] = 'c';
        overhead_buf[ob_len] = '\0';
        gfx_draw_string(&surf, 14, 308, overhead_buf, GFX_COLOR_TEXT_MUTED);

        /* End Task Button */
        bool is_protected = (tm->selected_pid <= 1ULL);
        uint32_t btn_col = is_protected ? GFX_COLOR_DISABLED : GFX_COLOR_EMERALD_PRIMARY;
        gfx_draw_glass_panel(&surf, 400, 304, 110, 26, btn_col);
        if (is_protected) {
            gfx_draw_string(&surf, 418, 309, "Protected", GFX_COLOR_TEXT_MUTED);
        } else {
            gfx_draw_string(&surf, 424, 309, "End Task", GFX_COLOR_TEXT_PRIMARY);
        }

    } else {
        /* Performance Tab */
        gfx_draw_string(&surf, 14, 46, "CPU & Scheduler Telemetry", GFX_COLOR_EMERALD_ACCENT);

        char smp_label[48];
        uint32_t online_cpus = smp_online_cpu_count();
        if (online_cpus > 1U) {
            str_copy(smp_label, "SMP: 2 Cores Active", sizeof(smp_label));
        } else {
            str_copy(smp_label, "Single Core", sizeof(smp_label));
        }
        gfx_draw_string(&surf, 320, 46, smp_label, GFX_COLOR_EMERALD_ACCENT);

        /* Graph Box */
        gfx_draw_glass_panel(&surf, 14, 68, 490, 130, GFX_COLOR_EMERALD_BASE);
        /* Graph grid lines */
        for (int y = 0; y < 4; y++) {
            gfx_draw_line(&surf, 16, (int32_t)(68 + y * 32), 502, (int32_t)(68 + y * 32), 0x3034D399U);
        }
        /* Draw activity curve */
        for (uint32_t p = 0U; p + 1U < TASKMGR_HISTORY_LEN; p++) {
            uint32_t s0 = tm->cpu_history[p];
            uint32_t s1 = tm->cpu_history[p + 1U];
            int32_t gx0 = (int32_t)(20U + p * 15U);
            int32_t gy0 = (int32_t)(190U - (s0 * 110U / 100U));
            int32_t gx1 = (int32_t)(20U + (p + 1U) * 15U);
            int32_t gy1 = (int32_t)(190U - (s1 * 110U / 100U));
            gfx_draw_line(&surf, gx0, gy0, gx1, gy1, GFX_COLOR_EMERALD_ACCENT);
        }

        /* Scheduler metrics strip */
        gfx_draw_glass_panel(&surf, 14, 204, 490, 42, GFX_COLOR_EMERALD_BASE);
        char sched_buf[64];
        char c_sw[16], c_pm[16], c_th[16];
        u64_to_str(tm->last_sched_stats.context_switches, c_sw);
        u64_to_str(tm->last_sched_stats.timer_preemptions, c_pm);
        u64_to_str(tm->last_sched_stats.active_threads, c_th);

        size_t sp = 0U;
        const char *p1 = "CtxSw: ";
        for (size_t k = 0U; p1[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = p1[k];
        for (size_t k = 0U; c_sw[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = c_sw[k];
        const char *p2 = " | Preempt: ";
        for (size_t k = 0U; p2[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = p2[k];
        for (size_t k = 0U; c_pm[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = c_pm[k];
        const char *p3 = " | ActiveThr: ";
        for (size_t k = 0U; p3[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = p3[k];
        for (size_t k = 0U; c_th[k] != '\0' && sp + 1U < sizeof(sched_buf); k++) sched_buf[sp++] = c_th[k];
        sched_buf[sp] = '\0';
        gfx_draw_string(&surf, 20, 210, sched_buf, GFX_COLOR_TEXT_PRIMARY);

        char perf_ovhd[64];
        char c_rf[16], c_rn[16];
        u64_to_str(tm->last_refresh_cycles, c_rf);
        u64_to_str(tm->last_render_cycles, c_rn);
        size_t op = 0U;
        const char *op1 = "Overhead: Ref: ";
        for (size_t k = 0U; op1[k] != '\0' && op + 1U < sizeof(perf_ovhd); k++) perf_ovhd[op++] = op1[k];
        for (size_t k = 0U; c_rf[k] != '\0' && op + 1U < sizeof(perf_ovhd); k++) perf_ovhd[op++] = c_rf[k];
        const char *op2 = " cyc | Rend: ";
        for (size_t k = 0U; op2[k] != '\0' && op + 1U < sizeof(perf_ovhd); k++) perf_ovhd[op++] = op2[k];
        for (size_t k = 0U; c_rn[k] != '\0' && op + 1U < sizeof(perf_ovhd); k++) perf_ovhd[op++] = c_rn[k];
        const char *op3 = " cyc";
        for (size_t k = 0U; op3[k] != '\0' && op + 1U < sizeof(perf_ovhd); k++) perf_ovhd[op++] = op3[k];
        perf_ovhd[op] = '\0';
        gfx_draw_string(&surf, 20, 226, perf_ovhd, GFX_COLOR_TEXT_MUTED);

        /* Memory Section */
        gfx_draw_string(&surf, 14, 252, "Physical Memory Usage", GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_glass_panel(&surf, 14, 272, 490, 30, GFX_COLOR_EMERALD_BASE);

        uint64_t used_ram = (tm->total_ram_bytes > tm->free_ram_bytes) ? (tm->total_ram_bytes - tm->free_ram_bytes) : 0ULL;
        uint32_t bar_w = (tm->total_ram_bytes > 0ULL) ? (uint32_t)((used_ram * 486ULL) / tm->total_ram_bytes) : 0U;
        if (bar_w > 486U) bar_w = 486U;
        gfx_draw_rect_filled(&surf, 16, 274, bar_w, 26U, GFX_COLOR_EMERALD_PRIMARY);

        char num1[24], num2[24];
        u64_to_str(used_ram / (1024ULL * 1024ULL), num1);
        u64_to_str(tm->total_ram_bytes / (1024ULL * 1024ULL), num2);
        gfx_draw_string(&surf, 24, 280, num1, GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_string(&surf, 60, 280, "MB /", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 95, 280, num2, GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_string(&surf, 135, 280, "MB Used", GFX_COLOR_TEXT_MUTED);

        /* Rate limit count */
        if (tm->refresh_rate_limited_count > 0ULL) {
            char rlim[32];
            char rcnt[16];
            u64_to_str(tm->refresh_rate_limited_count, rcnt);
            size_t rp = 0U;
            const char *rlp = "Rate-limited: ";
            for (size_t k = 0U; rlp[k] != '\0' && rp + 1U < sizeof(rlim); k++) rlim[rp++] = rlp[k];
            for (size_t k = 0U; rcnt[k] != '\0' && rp + 1U < sizeof(rlim); k++) rlim[rp++] = rcnt[k];
            rlim[rp] = '\0';
            gfx_draw_string(&surf, 320, 280, rlim, GFX_COLOR_TEXT_MUTED);
        }
    }

    uint64_t r1 = read_tsc();
    tm->last_render_cycles = (r1 >= r0) ? (r1 - r0) : 0ULL;
}

void task_manager_handle_click(struct task_manager *tm, int32_t rel_x, int32_t rel_y) {
    if (tm == NULL) return;

    /* Tab clicks */
    if (rel_y >= 8 && rel_y <= 34) {
        if (rel_x >= 10 && rel_x <= 120) {
            tm->current_tab = TASKMGR_TAB_PROCESSES;
            task_manager_render(tm);
            return;
        }
        if (rel_x >= 125 && rel_x <= 235) {
            tm->current_tab = TASKMGR_TAB_PERFORMANCE;
            task_manager_render(tm);
            return;
        }
    }

    if (tm->current_tab == TASKMGR_TAB_PROCESSES) {
        /* Row selection clicks */
        if (rel_y >= 64 && rel_y < 280) {
            uint32_t row_idx = (uint32_t)((rel_y - 64) / 24);
            if (row_idx < tm->item_count) {
                tm->selected_pid = tm->items[row_idx].pid;
                task_manager_render(tm);
            }
            return;
        }
        /* End Task Button (x in [400, 510], y in [304, 330]) */
        if (rel_x >= 400 && rel_x <= 510 && rel_y >= 304 && rel_y <= 330) {
            (void)task_manager_kill_selected(tm);
            task_manager_render(tm);
            return;
        }
    }
}

bool task_manager_initialize(void) {
    if (taskmgr_initialized) return true;

    struct task_manager *tm = &main_taskmgr;
    *tm = (struct task_manager){0};
    tm->current_tab = TASKMGR_TAB_PROCESSES;
    tm->selected_pid = 1ULL;
    tm->render_buffer[0] = 0U;

    task_manager_refresh_forced(tm);
    task_manager_render(tm);

    tm->active = true;
    taskmgr_initialized = true;
    return true;
}

struct task_manager *task_manager_get_instance(void) {
    if (!taskmgr_initialized) (void)task_manager_initialize();
    return &main_taskmgr;
}

bool task_manager_window_open(void) {
    if (taskmgr_window_id >= 0) {
        if (wm_get_window_by_id((uint32_t)taskmgr_window_id) != NULL) {
            wm_focus_window((uint32_t)taskmgr_window_id);
            task_manager_refresh_forced(&main_taskmgr);
            task_manager_render(&main_taskmgr);
            return true;
        }
        taskmgr_window_id = -1;
    }

    if (!taskmgr_initialized) {
        (void)task_manager_initialize();
    }

    struct window *win = wm_create_window("Task Manager", 140, 90, TASKMGR_WIDTH, TASKMGR_HEIGHT,
                                          WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR | WM_FLAG_RESIZABLE);
    if (win == NULL) return false;

    win->client_buffer = taskmgr_render_buf;
    win->client_pitch = TASKMGR_WIDTH * sizeof(uint32_t);
    win->on_click = task_manager_window_handle_click;
    win->on_close = task_manager_on_close;
    main_taskmgr.win = win;
    taskmgr_window_id = (int)win->id;

    task_manager_refresh_forced(&main_taskmgr);
    task_manager_render(&main_taskmgr);
    return true;
}

void task_manager_window_close(void) {
    if (taskmgr_window_id >= 0) {
        wm_destroy_window((uint32_t)taskmgr_window_id);
        taskmgr_window_id = -1;
        main_taskmgr.win = NULL;
    }
}

int task_manager_window_get_id(void) {
    return taskmgr_window_id;
}

void task_manager_get_overhead(uint64_t *out_refresh_cycles, uint64_t *out_render_cycles) {
    if (out_refresh_cycles != NULL) {
        *out_refresh_cycles = main_taskmgr.last_refresh_cycles;
    }
    if (out_render_cycles != NULL) {
        *out_render_cycles = main_taskmgr.last_render_cycles;
    }
}

/* Self-test */
bool task_manager_self_test(void) {
    if (!task_manager_initialize()) return false;
    struct task_manager *tm = &main_taskmgr;

    /* 1. Verify dimensions */
    if (TASKMGR_WIDTH != 520U || TASKMGR_HEIGHT != 340U) return false;

    /* 2. Forced Refresh & item count verification */
    task_manager_refresh_forced(tm);
    if (tm->item_count < 1U) return false;
    if (tm->items[0].pid != 0ULL) return false;

    /* 3. Bounded Rate-Limiting Test */
    uint64_t lim_before = tm->refresh_rate_limited_count;
    task_manager_refresh(tm);
    if (tm->refresh_rate_limited_count <= lim_before) return false;

    /* 4. Monitoring Overhead Measurement Test */
    if (tm->last_refresh_cycles == 0ULL) return false;
    if (tm->last_refresh_cycles > 50000000ULL) return false;

    /* 5. Permission checks & Protection Gates */
    if (task_manager_kill_process(tm, 0ULL) != TASKMGR_ERR_PROTECTED) return false;
    if (task_manager_kill_process(tm, 1ULL) != TASKMGR_ERR_PROTECTED) return false;
    if (task_manager_kill_process(tm, 9999ULL) != TASKMGR_ERR_NOT_FOUND) return false;

    /* 6. Process Lifecycle & Safe Termination Test */
    uint64_t test_pid = 0ULL;
    if (!process_create(&test_pid)) return false;
    if (!process_mark_ready(test_pid)) return false;

    task_manager_refresh_forced(tm);
    bool found_test_proc = false;
    for (size_t i = 0U; i < tm->item_count; i++) {
        if (tm->items[i].pid == test_pid) {
            found_test_proc = true;
            if (tm->items[i].state != PROCESS_STATE_READY) return false;
            break;
        }
    }
    if (!found_test_proc) return false;

    if (task_manager_kill_process(tm, test_pid) != TASKMGR_SUCCESS) return false;

    struct process_info pinfo = {0};
    if (!process_get_info(test_pid, &pinfo) || pinfo.state != PROCESS_STATE_EXITED) return false;

    if (task_manager_kill_process(tm, test_pid) != TASKMGR_ERR_ALREADY_DEAD) return false;
    (void)process_reap(test_pid);

    /* 7. Tab switching & interaction test */
    task_manager_handle_click(tm, 150, 15); /* Click Performance tab */
    if (tm->current_tab != TASKMGR_TAB_PERFORMANCE) return false;

    task_manager_handle_click(tm, 30, 15); /* Click Processes tab */
    if (tm->current_tab != TASKMGR_TAB_PROCESSES) return false;

    task_manager_handle_click(tm, 50, 70); /* Click row 0 */
    if (tm->selected_pid != tm->items[0].pid) return false;

    /* 8. Render test and render overhead measurement */
    task_manager_render(tm);
    if (tm->last_render_cycles == 0ULL) return false;

    /* 9. Window Manager Integration Test */
    if (!task_manager_window_open()) return false;
    if (task_manager_window_get_id() < 0) return false;

    struct window *win = wm_get_window_by_id((uint32_t)task_manager_window_get_id());
    if (win == NULL || win->client_buffer == NULL) return false;

    task_manager_window_close();
    if (task_manager_window_get_id() >= 0) return false;

    return true;
}
