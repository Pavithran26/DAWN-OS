/* SPDX-License-Identifier: Unlicense */
#include "dawn_gui.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    dawn_println("DAWN-OS Graphical Client Demo starting in Ring 3...");

    dawn_window_t win = dawn_gui_create_window("Emerald App", 100, 100, 400, 300);
    if (win == NULL) {
        dawn_println("Error: Failed to create graphical window.");
        return 1;
    }

    /* Fill background with Emerald Glass Panel */
    dawn_gui_clear(win, DAWN_GUI_COLOR_EMERALD_BASE);

    /* Draw Titlebar */
    dawn_gui_draw_rect(win, 0, 0, 400, 30, DAWN_GUI_COLOR_EMERALD_PRIMARY);
    dawn_gui_draw_text(win, 12, 10, "DAWN User App", DAWN_GUI_COLOR_TEXT_PRIMARY);

    /* Draw Interactive Button */
    dawn_gui_draw_rect(win, 50, 60, 120, 36, DAWN_GUI_COLOR_EMERALD_ACCENT);
    dawn_gui_draw_text(win, 65, 72, "Click Me", DAWN_GUI_COLOR_TEXT_PRIMARY);

    /* Draw Status Text */
    dawn_gui_draw_rect(win, 50, 120, 300, 120, DAWN_GUI_COLOR_GLASS_PANEL);
    dawn_gui_draw_text(win, 60, 140, "Pure Scratch POSIX Ring 3 GUI", DAWN_GUI_COLOR_TEXT_PRIMARY);

    /* Commit buffer to display server */
    dawn_gui_commit(win);
    dawn_println("DAWN-OS Graphical Window committed to compositor successfully.");

    /* Clean up */
    dawn_gui_destroy_window(win);
    return 0;
}
