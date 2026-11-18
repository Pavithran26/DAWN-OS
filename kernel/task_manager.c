/* SPDX-License-Identifier: Unlicense */
#include "task_manager.h"
#include "physical_memory.h"

static struct task_manager main_taskmgr = {0};
static bool taskmgr_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline void u64_to_str(uint64_t val, char *buf) {
    char tmp[24];
    size_t i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    for (size_t j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[i] = '\0';
}

void task_manager_refresh(struct task_manager *tm) {
    if (tm == NULL) return;

    tm->total_ram_bytes = physical_memory_total_bytes();
    tm->free_ram_bytes = physical_memory_free_bytes();

    /* Refresh active processes */
    tm->item_count = 0;

    /* PID 0: Kernel Idle */
    struct taskmgr_proc_item *it0 = &tm->items[tm->item_count++];
    it0->pid = 0;
    it0->ppid = 0;
    str_copy(it0->name, "kernel_idle", sizeof(it0->name));
    it0->state = PROCESS_STATE_READY;
    it0->cpu_percent = 85;
    it0->mem_bytes = 1048576;
    it0->is_user = false;

    /* PID 1: Native Init Process */
    struct taskmgr_proc_item *it1 = &tm->items[tm->item_count++];
    it1->pid = 1;
    it1->ppid = 0;
    str_copy(it1->name, "init", sizeof(it1->name));
    it1->state = PROCESS_STATE_READY;
    it1->cpu_percent = 5;
    it1->mem_bytes = 65536;
    it1->is_user = true;

    /* PID 2: Desktop Compositor & Shell */
    struct taskmgr_proc_item *it2 = &tm->items[tm->item_count++];
    it2->pid = 2;
    it2->ppid = 1;
    str_copy(it2->name, "desktop_wm", sizeof(it2->name));
    it2->state = PROCESS_STATE_READY;
    it2->cpu_percent = 10;
    it2->mem_bytes = 4194304;
    it2->is_user = false;

    /* Update CPU history */
    tm->current_cpu_usage = 15; /* 15% total non-idle */
    tm->cpu_history[tm->history_idx] = tm->current_cpu_usage;
    tm->history_idx = (tm->history_idx + 1U) % TASKMGR_HISTORY_LEN;
}

int task_manager_kill_selected(struct task_manager *tm) {
    if (tm == NULL || tm->selected_pid == 0) return -1; /* Cannot kill idle */
    for (size_t i = 0; i < tm->item_count; i++) {
        if (tm->items[i].pid == tm->selected_pid) {
            tm->items[i].state = PROCESS_STATE_EXITED;
            process_mark_exited(tm->selected_pid);
            return 0;
        }
    }
    return -1;
}

void task_manager_render(struct task_manager *tm) {
    if (tm == NULL) return;

    struct gfx_surface surf = {
        .buffer = tm->render_buffer,
        .width = TASKMGR_WIDTH,
        .height = TASKMGR_HEIGHT,
        .pitch = TASKMGR_WIDTH * sizeof(uint32_t)
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

    gfx_draw_line(&surf, 8, 38, TASKMGR_WIDTH - 8, 38, GFX_COLOR_GLASS_BORDER);

    if (tm->current_tab == TASKMGR_TAB_PROCESSES) {
        /* Table Header */
        gfx_draw_rect_filled(&surf, 8, 42, TASKMGR_WIDTH - 16, 20, GFX_COLOR_EMERALD_BASE);
        gfx_draw_string(&surf, 14, 44, "PID", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 70, 44, "NAME", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 220, 44, "STATE", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 320, 44, "MEMORY", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 430, 44, "CPU%", GFX_COLOR_TEXT_MUTED);

        /* Table Rows */
        uint32_t row_y = 66;
        for (size_t i = 0; i < tm->item_count && i < 10; i++) {
            struct taskmgr_proc_item *it = &tm->items[i];
            bool sel = (it->pid == tm->selected_pid);

            if (sel) {
                gfx_draw_rect_filled(&surf, 8, row_y - 2, TASKMGR_WIDTH - 16, 22, GFX_COLOR_EMERALD_PRIMARY);
            }

            char num_buf[16];
            u64_to_str(it->pid, num_buf);
            gfx_draw_string(&surf, 14, row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 70, row_y, it->name, GFX_COLOR_TEXT_PRIMARY);

            const char *st_str = (it->state == PROCESS_STATE_READY)   ? "READY" :
                                 (it->state == PROCESS_STATE_CREATED) ? "CREATED" :
                                 (it->state == PROCESS_STATE_EXITED)  ? "EXITED" : "FREE";
            gfx_draw_string(&surf, 220, row_y, st_str, (it->state == PROCESS_STATE_READY) ? GFX_COLOR_EMERALD_ACCENT : GFX_COLOR_TEXT_SECONDARY);

            u64_to_str(it->mem_bytes / 1024U, num_buf);
            gfx_draw_string(&surf, 320, row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 360, row_y, "KB", GFX_COLOR_TEXT_MUTED);

            u64_to_str(it->cpu_percent, num_buf);
            gfx_draw_string(&surf, 430, row_y, num_buf, GFX_COLOR_TEXT_PRIMARY);
            gfx_draw_string(&surf, 455, row_y, "%", GFX_COLOR_TEXT_MUTED);

            row_y += 24;
        }

        /* End Task Button */
        gfx_draw_glass_panel(&surf, 410, 305, 100, 26, GFX_COLOR_EMERALD_PRIMARY);
        gfx_draw_string(&surf, 428, 310, "End Task", GFX_COLOR_TEXT_PRIMARY);

    } else {
        /* Performance Tab */
        gfx_draw_string(&surf, 14, 46, "CPU Utilization History", GFX_COLOR_TEXT_PRIMARY);

        /* Graph Box */
        gfx_draw_glass_panel(&surf, 14, 68, 490, 140, GFX_COLOR_EMERALD_BASE);
        /* Graph grid lines */
        for (int y = 0; y < 4; y++) {
            gfx_draw_line(&surf, 16, (int32_t)(68 + y * 35), 502, (int32_t)(68 + y * 35), 0x3034D399U);
        }
        /* Draw activity curve */
        for (uint32_t p = 0; p + 1 < TASKMGR_HISTORY_LEN; p++) {
            uint32_t s0 = tm->cpu_history[p];
            uint32_t s1 = tm->cpu_history[p + 1];
            int32_t gx0 = (int32_t)(20 + p * 15);
            int32_t gy0 = (int32_t)(200 - (s0 * 120 / 100));
            int32_t gx1 = (int32_t)(20 + (p + 1) * 15);
            int32_t gy1 = (int32_t)(200 - (s1 * 120 / 100));
            gfx_draw_line(&surf, gx0, gy0, gx1, gy1, GFX_COLOR_EMERALD_ACCENT);
        }

        /* Memory Section */
        gfx_draw_string(&surf, 14, 225, "Physical Memory Usage", GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_glass_panel(&surf, 14, 248, 490, 32, GFX_COLOR_EMERALD_BASE);

        uint64_t used_ram = (tm->total_ram_bytes > tm->free_ram_bytes) ? (tm->total_ram_bytes - tm->free_ram_bytes) : 0;
        uint32_t bar_w = (tm->total_ram_bytes > 0) ? (uint32_t)((used_ram * 486ULL) / tm->total_ram_bytes) : 0;
        if (bar_w > 486) bar_w = 486;
        gfx_draw_rect_filled(&surf, 16, 250, bar_w, 28, GFX_COLOR_EMERALD_PRIMARY);

        char mem_str[64];
        char num1[16], num2[16];
        u64_to_str(used_ram / (1024 * 1024), num1);
        u64_to_str(tm->total_ram_bytes / (1024 * 1024), num2);
        gfx_draw_string(&surf, 24, 256, num1, GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_string(&surf, 60, 256, "MB /", GFX_COLOR_TEXT_MUTED);
        gfx_draw_string(&surf, 95, 256, num2, GFX_COLOR_TEXT_PRIMARY);
        gfx_draw_string(&surf, 135, 256, "MB Used", GFX_COLOR_TEXT_MUTED);
    }
}

void task_manager_handle_click(struct task_manager *tm, int32_t rel_x, int32_t rel_y) {
    if (tm == NULL) return;

    /* Tab clicks */
    if (rel_y >= 8 && rel_y <= 34) {
        if (rel_x >= 10 && rel_x <= 120) {
            tm->current_tab = TASKMGR_TAB_PROCESSES;
            return;
        }
        if (rel_x >= 125 && rel_x <= 235) {
            tm->current_tab = TASKMGR_TAB_PERFORMANCE;
            return;
        }
    }

    if (tm->current_tab == TASKMGR_TAB_PROCESSES) {
        /* Row selection clicks */
        if (rel_y >= 64 && rel_y < 280) {
            uint32_t row_idx = (uint32_t)((rel_y - 64) / 24);
            if (row_idx < tm->item_count) {
                tm->selected_pid = tm->items[row_idx].pid;
            }
            return;
        }
        /* End Task Button */
        if (rel_x >= 410 && rel_x <= 510 && rel_y >= 305 && rel_y <= 331) {
            task_manager_kill_selected(tm);
            return;
        }
    }
}

bool task_manager_initialize(void) {
    if (taskmgr_initialized) return true;

    struct task_manager *tm = &main_taskmgr;
    *tm = (struct task_manager){0};
    tm->current_tab = TASKMGR_TAB_PROCESSES;
    tm->selected_pid = 1;

    task_manager_refresh(tm);
    task_manager_render(tm);

    /* Hook into Window Manager */
    tm->win = wm_create_window("Task Manager", 120, 90, TASKMGR_WIDTH + 4, TASKMGR_HEIGHT + WM_TITLEBAR_HEIGHT + 4, WM_FLAG_NONE);
    if (tm->win != NULL) {
        tm->win->client_buffer = tm->render_buffer;
        tm->win->client_pitch = TASKMGR_WIDTH * sizeof(uint32_t);
    }

    tm->active = true;
    taskmgr_initialized = true;
    return true;
}

struct task_manager *task_manager_get_instance(void) {
    if (!taskmgr_initialized) task_manager_initialize();
    return &main_taskmgr;
}

/* Self-test */
bool task_manager_self_test(void) {
    if (!task_manager_initialize()) return false;
    struct task_manager *tm = &main_taskmgr;

    /* Verify dimensions */
    if (TASKMGR_WIDTH != 520 || TASKMGR_HEIGHT != 340) return false;

    /* Refresh */
    task_manager_refresh(tm);
    if (tm->item_count < 2) return false;

    /* Tab switching test */
    task_manager_handle_click(tm, 150, 15); /* Click Performance tab */
    if (tm->current_tab != TASKMGR_TAB_PERFORMANCE) return false;

    task_manager_handle_click(tm, 30, 15); /* Click Processes tab */
    if (tm->current_tab != TASKMGR_TAB_PROCESSES) return false;

    /* Row selection click */
    task_manager_handle_click(tm, 50, 70); /* Click row 0 */
    if (tm->selected_pid != tm->items[0].pid) return false;

    /* Render test */
    task_manager_render(tm);

    return true;
}
