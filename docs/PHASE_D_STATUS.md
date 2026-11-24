# Phase D — Graphical Desktop Shell & Safe Framebuffer Renderer Status

## 1. Phase Summary

Phase D implements the optional local-console graphical desktop shell for DAWN-OS, strictly governed by `docs/PHASE_D_PLAN.md` and downstream of the validated Phase B (kernel memory & CPU foundations) and Phase C (userspace runtime & storage integration) milestones. Headless/server operation remains the core default, while graphical console mode provides an emerald-green liquid-neomorphic administrative interface.

---

## 2. Milestone D0 — Desktop Specification & Design System

### Design Tokens (`include/gfx.h`)
- **Emerald Green Liquid-Neomorphism Palette (ARGB8888):**
  - **Surfaces & Canvas:**
    - Deep Background: `GFX_COLOR_EMERALD_DEEP` (`0xFF051B11U`)
    - Base Canvas: `GFX_COLOR_EMERALD_BASE` (`0xFF0A2E1FU`)
    - Inset Surface: `GFX_COLOR_EMERALD_SURFACE` (`0xFF0E3D2AU`)
    - Primary Accent: `GFX_COLOR_EMERALD_PRIMARY` (`0xFF0F5A3BU`)
    - Neon Accent: `GFX_COLOR_EMERALD_ACCENT` (`0xFF10B981U`)
    - Specular Highlight / Glow: `GFX_COLOR_EMERALD_HIGHLIGHT` (`0xFF34D399U`)
    - Mint / Cyan Accent: `GFX_COLOR_EMERALD_CYAN` (`0xFF06D6A0U`)
  - **Glass & Translucency:**
    - Translucent Frosted Panel: `GFX_COLOR_GLASS_PANEL` (`0xD00D281EU`)
    - Semi-Translucent Surface: `GFX_COLOR_GLASS_SURFACE` (`0xC00A251BU`)
    - Specular Glass Border: `GFX_COLOR_GLASS_BORDER` (`0x6034D399U`)
    - Ambient Drop Shadow: `GFX_COLOR_GLASS_SHADOW` (`0x80000000U`)
  - **Typography & States:**
    - Crisp Off-White Text: `GFX_COLOR_TEXT_PRIMARY` (`0xFFF0FDF4U`)
    - Sage Secondary Text: `GFX_COLOR_TEXT_SECONDARY` (`0xFFA7F3D0U`)
    - Dimmed Muted Text: `GFX_COLOR_TEXT_MUTED` (`0xFF6EE7B7U`)
    - High-Contrast Focus Ring: `GFX_COLOR_FOCUS_RING` (`0xFF38BDF8U`)
    - Muted Disabled Control: `GFX_COLOR_DISABLED` (`0x80374151U`)
    - Semantic Indicators: Warning Amber (`0xFFF59E0BU`), Error Ruby (`0xFFEF4444U`), Success Emerald (`0xFF10B981U`)
  - **Window Chrome Controls:**
    - Close: Ruby Red (`0xFFEF4444U`)
    - Minimize: Amber (`0xFFF59E0BU`)
    - Maximize: Emerald (`0xFF10B981U`)

- **Spatial & Radii Tokens (4px / 8px Grid):**
  - Spacing: `GFX_SPACE_XS` (2px), `GFX_SPACE_SM` (4px), `GFX_SPACE_MD` (8px), `GFX_SPACE_LG` (16px), `GFX_SPACE_XL` (24px), `GFX_SPACE_2XL` (32px).
  - Radii: `GFX_RADIUS_SM` (4px buttons/badges), `GFX_RADIUS_MD` (8px panels/controls), `GFX_RADIUS_LG` (12px dialogs/windows).

- **Desktop Geometry & Layout Contracts:**
  - Screen resolution range: 640x480 minimum to 7680x4320 (8K) maximum; standard target 1280x800 / 1920x1080.
  - Top Status Bar: Height 28px (`DESKTOP_STATUSBAR_HEIGHT = 28U`), glass frosted body, workspace indicator, system metrics, RTC clock, and session status.
  - Bottom Dock / Launcher: Height 48px (`DESKTOP_TASKBAR_HEIGHT = 48U`), pinned app icons with glowing active indicators.
  - Window Chrome: Titlebar height 28px (`WM_TITLEBAR_HEIGHT = 28U`), border width 2px (`WM_BORDER_WIDTH = 2U`), minimum client dimensions 160x100.
  - Notifications: Stacked toast panels (up to 4 active) anchored at top-right (width 244px, height 52px), timeout countdown, and smooth dismiss.
  - Keyboard Navigation & Motion: Tab/Shift-Tab focus traversal, Enter to activate, Esc to dismiss, Super/Meta for launcher; instantaneous state updates under reduced-motion mode.

---

## 3. Milestone D1 — Safe Framebuffer & CPU 2D Renderer

### Framebuffer Metadata Validation (`kernel/gfx.c`, `include/gfx.h`)
- Implemented `gfx_validate_framebuffer(const struct limine_framebuffer *lfb)`:
  - Validates non-null framebuffer structure and memory address.
  - Enforces canonical supervisor higher-half address check (`(uintptr_t)address >= 0xFFFF800000000000ULL`).
  - Rejects zero width and height; bounds dimensions within `[640..7680] x [480..4320]`.
  - Rejects non-32 bpp modes (only 32 bpp linear framebuffers supported).
  - Requires scanline pitch at least `width * 4` bytes and strictly 4-byte aligned.
  - Checks 64-bit multiplication overflow (`pitch * height`) and limits total size to 256 MiB.
  - Validates 8-bit standard channel mask sizes and resolves pixel layout (`GFX_FORMAT_ARGB8888` vs `GFX_FORMAT_ABGR8888`).
  - Safely falls back to internal memory-backed backbuffer if metadata is invalid, preventing unsafe writes to raw video memory.

### Safe Bounded 2D Primitives
- **`struct gfx_surface`**:
  - Contains `buffer`, `width`, `height`, `pitch`, `buffer_size` (in bytes), `is_hardware_fb`, and `format`.
  - Verified by `gfx_surface_is_valid()` before any memory access.
- **Checked Pixel Plotting & Alpha Blending:**
  - `gfx_put_pixel`: Boundary checks `x < surf->width && y < surf->height`, buffer size offset bounds, and format color packing.
  - `gfx_blend_pixel`: Fixed-point alpha blending `((color * a + bg * (255 - a)) >> 8)` with zero integer overflow risk.
- **Bounded Clipping & Integer Overflow Prevention:**
  - `gfx_draw_rect_filled`: Uses subtraction-based clipping `if (w > surf->width - x) w = surf->width - x` to eliminate `uint32_t` arithmetic wrap-around vulnerability when huge widths/heights (e.g. `0xFFFFFFFF`) are passed.
  - `gfx_draw_rect_filled_signed`: Clips negative coordinates `(x < 0, y < 0)` safely against screen origin.
  - `gfx_draw_rect_outline`: Perimeter drawing with safe bounds subtraction.
  - `gfx_draw_line`: Bresenham line algorithm with per-pixel clipping checks.
  - `gfx_draw_char` / `gfx_draw_string`: Bounded glyph rendering with monospace font 8x16, newline handling, and clipping at screen edges.

### Clipped Blit Primitives
- **`gfx_blit(dst, dst_x, dst_y, src, src_x, src_y, w, h)`**:
  - Two-sided clipping: clips source rectangle to source surface dimensions, and clips destination rectangle to destination surface dimensions.
  - Handles negative destination offsets (`dst_x < 0`, `dst_y < 0`) by sliding source offsets and reducing width/height.
  - Re-formats color channels on the fly if source and destination have mismatched formats (e.g., ARGB to ABGR).
  - Row-by-row memory copy respecting pitch and buffer bounds.
- **`gfx_blit_alpha`**:
  - Combines clipped blitting with alpha-blending for translucent glass panels and floating UI components.

### Bounded Dirty Region Damage Tracking & Scheduling
- Keeps rendering out of interrupt context: drawing executes exclusively in scheduler/task context.
- Maintains `struct gfx_damage` recording dirty bounding box (`min_x`, `min_y`, `max_x`, `max_y`).
- `gfx_damage_add(x, y, w, h)`: Expands bounding box, clipped to backbuffer resolution.
- `gfx_damage_add_fullscreen()`: Marks entire backbuffer dirty.
- `gfx_flush()`:
  - If not dirty: no-op, zero memory copies performed.
  - If dirty: uses `gfx_blit` to copy only the damaged sub-rectangle to the hardware framebuffer.
  - Clears dirty flag.
  - Drastically conserves memory bus bandwidth on virtual and physical hardware.

### Deterministic Headless Test Harness
- `gfx_self_test()` verifies:
  1. Rejection of null framebuffer pointer, null address, lower canonical address, zero width/height, non-32 bpp, unaligned pitch, undersized pitch, and multiplication overflow.
  2. Acceptance of valid synthetic and live Limine framebuffer metadata.
  3. Safe surface creation and parameter validation.
  4. Pixel plotting and 50% alpha blend accuracy.
  5. Overflow-proof clipped rect fill with `0xFFFFFFFF` dimensions.
  6. Signed rect clipping with negative coordinates.
  7. Full and clipped `gfx_blit` with negative destination coordinates, offscreen blits, and `gfx_blit_alpha`.
  8. Damage tracking accumulation, bounding box expansion, and `gfx_flush` lifecycle.
  9. Glyph rendering and boundary text clipping.
  10. Glass panel and icon generation.

---

## 4. Milestone D2 — Input & Event Delivery

### Normalized Event Architecture (`include/input.h`, `kernel/input.c`)
- **Bounded Event Queue (`INPUT_QUEUE_CAPACITY = 128U`):**
  - Ring buffer decouples interrupt-side capture (`ps2_poll_key`, `ps2_poll_mouse`, USB HID) from desktop composition and UI-side event handling.
  - Drop-tail overflow policy: when the queue reaches capacity (128 events), newly pushed events are rejected immediately without blocking, allocating, or corrupting state.
  - Telemetry and drop counters track system health: `input_get_total_events()`, `input_get_dropped_events()`, `input_get_overflow_count()`, and `input_get_queue_count()`.
- **Interrupt-Safe Concurrency:**
  - Push and poll operations are synchronized using `cpu_save_flags_and_cli()` and `cpu_restore_flags()` to ensure re-entrancy safety across timer ticks and device interrupts.
- **Mouse Motion Coalescing:**
  - Consecutive motion events with identical button states are coalesced into the latest queue slot (`dx += evt->dx`, `dy += evt->dy`, `x = evt->x`, `y = evt->y`).
  - Prevents high-frequency pointer movement from exhausting the bounded queue and starving keyboard events.

### Hardened PS/2 Keyboard & Mouse Driver (`include/ps2.h`, `kernel/ps2.c`)
- **Extended Scancode Decoding (`0xE0` prefix):**
  - Full decoding of navigational arrow keys (`KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`), Delete (`KEY_DELETE`), Super/Meta (`KEY_SUPER`), and right modifier keys.
  - Tracking of GUI/Super modifier state in `struct key_event` (`bool gui`).
- **Floating Bus / Absent Device Detection:**
  - Status port checks immediately detect floating bus (`0xFF`) and break out of read/write wait loops without spinning for 100,000 cycles.
  - Non-blocking polling returns immediately when no hardware is present or ready.
- **Mouse Packet Validation:**
  - Rejection of packets lacking the standard PS/2 sync bit (`(byte & 0x08U) == 0`).
  - Rejection of packets reporting X/Y overflow (`0x40` or `0x80` status bits).

### Keyboard-First Navigation & Focus Indication (`kernel/window_manager.c`, `kernel/desktop.c`)
- **Window Focus Cycling (`Alt+Tab` / `Alt+Shift+Tab`):**
  - `wm_cycle_focus()` cycles the active window across the Z-order hierarchy.
  - Automatically un-minimizes the focused window upon activation.
  - Dispatches `wm_handle_key` to focused window callbacks (`on_key`).
- **Shortcut Controls (`Alt+F4`, `Super`, `Ctrl+Esc`):**
  - `Alt+F4`: Safely closes the top active window and refocuses the next window in Z-order.
  - `Super` / `Ctrl+Esc`: Toggles the application launcher open and closed.
  - `Esc`: Dismisses the open application launcher.
- **High-Contrast Focus Indication:**
  - Active window is bordered by a distinct focus ring outline (`GFX_COLOR_FOCUS_RING = 0xFF38BDF8U`) during composition.

### Test Harness & Validation
- `ps2_self_test()`: Extended key decoding, Super key press/release, mouse packet decoding, bad sync bit rejection, overflow rejection, and coordinate boundary clamping.
- `input_self_test()`: FIFO event ordering, mouse motion coalescing, and 128-element queue capacity overflow rejection.
- `wm_self_test()`: Z-order traversal, `wm_cycle_focus`, `Alt+Tab`, and `Alt+F4` active window closure.
- `desktop_self_test()`: Super key toggle, Ctrl+Esc launcher open, and Esc dismissal.

---

## 5. Milestone D3 — Compositor & Window Manager

### Desktop Shell & Top Status Bar (`kernel/desktop.c`, `include/desktop.h`)
- **Top Status Bar (`DESKTOP_STATUSBAR_HEIGHT = 28U`):**
  - Frosted glass body (`GFX_COLOR_GLASS_SURFACE`) spanning `y = 0` to `y = 28` with a specular neon emerald lower accent line (`GFX_COLOR_EMERALD_ACCENT`).
  - Left: System logo and workspace switcher pills (`[1]`, `[2]`, `[3]`, `[4]`). Active workspace rendered with neon border outline and emerald primary fill.
  - Center: Dynamic system telemetry (`CPU: 0.2% | RAM: 24M/128M`).
  - Right: Administrative session status (`admin@dawn`).
- **Bottom Dock / Taskbar (`DESKTOP_TASKBAR_HEIGHT = 48U`):**
  - Emerald base panel with frosted glass overlay and neon accent line.
  - Interactive Start button toggling the liquid-neomorphic application launcher.
  - Workspace-scoped window switcher tabs with glowing active underline for focused window.
  - System tray with dynamic RTC clock in a dedicated glass pill container.
  - Central metadata footer with clickable developer website link.
- **Desktop Icons & Background Canvas:**
  - Full-screen emerald vertical gradient background (`GFX_COLOR_EMERALD_BASE` to `GFX_COLOR_EMERALD_DEEP`).
  - Vertically offset desktop icons (`y >= 28`) to prevent collision with top status bar.

### Multi-Workspace State Management (`kernel/window_manager.c`, `include/window_manager.h`)
- **4 Virtual Workspaces (`WM_MAX_WORKSPACES = 4U`):**
  - Windows are tagged with target workspace index (`win->workspace`).
  - `wm_get_current_workspace()`, `wm_set_current_workspace(ws)`, `wm_move_window_to_workspace(id, ws)`.
  - Windows on non-active workspaces are hidden from composition and hit-testing unless flagged with `WM_FLAG_ALL_WORKSPACES`.
  - Switching workspaces automatically refocuses the highest-priority window on the target workspace.
  - Navigation: Status bar pill clicks, `Super + 1..4` direct jumps, and `Ctrl + Alt + Left/Right` sequential cycling.

### Window Lifecycle, Geometry & Interactive Resizing
- **Interactive Resizing (`WM_FLAG_RESIZABLE`):**
  - Resizable windows feature a tactile 3-line diagonal grip in the bottom-right corner.
  - Hit testing detects drags in the 16x16 corner grip and resizes the window dynamically.
  - Dimension constraints: clamped against `min_width` (100px) and `min_height` (60px).
- **Desktop Boundary-Constrained Maximization:**
  - Maximized windows automatically expand between the top status bar (`y = 28`) and bottom taskbar (`height = screen_height - 76`), keeping window titlebars and taskbar tabs accessible.
  - Restore toggles back to previous saved geometry (`saved_x`, `saved_y`, `saved_width`, `saved_height`).
- **Window Movement & Dragging:**
  - Click-and-drag titlebar movement with negative coordinate clipping against display bounds.
- **Window Minimization:**
  - Minimizing hides window and un-focuses it; clicking taskbar tab restores and focuses.

### Damage Tracking & Bounded Redraw Scheduling
- Zero dynamic memory allocations during hot rendering paths.
- `wm_mark_window_damaged()`: Records bounding damage boxes on window creation, movement, resizing, state change, and destruction.
- `gfx_damage_add()` and `gfx_flush()`: Flush only damaged sub-rectangles from the backbuffer to the physical framebuffer.

### Test Harness & Validation
- `wm_self_test()`: Multi-window creation, Z-order manipulation, window movement, resize API and minimum bounds clamping, bottom-right mouse resize grip interaction, status-bar-bounded maximization, minimization/restore, multi-workspace isolation (`wm_set_current_workspace`, `wm_move_window_to_workspace`), `Alt+Tab` focus cycling, and `Alt+F4` destruction.
- `desktop_self_test()`: Top status bar rendering, neon lower accent line verification at `y = 27`, workspace pill mouse click switching, `Super + 1` keyboard jump, `Ctrl + Alt + Left/Right` keyboard traversal, damage tracking dirty accumulation, and `gfx_flush()` redraw cycle.

---

## 6. Milestone D4 — Terminal Application

### Architecture & Terminal Emulator (`kernel/terminal.c`, `include/terminal.h`)
- **Grid & Geometry:**
  - Standard 80x25 monospace character grid (`TERM_COLS = 80U`, `TERM_ROWS = 25U`).
  - Total client resolution: 640x400 (`TERM_PIXEL_WIDTH = 640U`, `TERM_PIXEL_HEIGHT = 400U`).
  - Cell representation (`struct term_cell`): glyph character, foreground ARGB8888 color, background ARGB8888 color.
- **Bounded Circular Scrollback Ring Buffer (`TERM_SCROLLBACK_ROWS = 128U`):**
  - Zero dynamic allocations: statically bounded ring buffer in kernel memory.
  - As lines scroll off row 0, they are pushed into `scrollback[TERM_SCROLLBACK_ROWS][TERM_COLS]`.
  - `terminal_scroll(term, delta)`: adjust visible viewport offset (`scroll_offset`), clamped between 0 and `scrollback_count`.
  - `terminal_scroll_to_bottom(term)`: snaps viewport to live input prompt.
  - Interactive scroll viewport navigation via `Shift + Up/Down`, `Shift + PageUp/PageDown`, and `Shift + Home/End`.
  - Tactile visual feedback: frosted glass pill badge `[^ +<N>]` rendered in top-right corner when viewing historical scrollback.
- **ANSI Escape Sequence Parser (CSI):**
  - Robust parameter scanner recognizing SGR color attributes (`\033[0m`, `\033[1m`, `\033[31m`..`\033[37m`).
  - Clear screen sequence `\033[2J` invoking `terminal_clear()` and cursor home `\033[H`.
  - Line erase sequence `\033[K` clearing from cursor to right margin.
- **Window Manager & Desktop Integration:**
  - `terminal_window_open()`: creates or focuses the native `"Terminal"` desktop window.
  - Registers `on_key` callback (`terminal_on_window_key`) to route window keyboard events directly to the terminal emulator.
  - Registers `on_close` callback (`terminal_on_window_close`) to safely decouple the active window reference while preserving session state, scrollback, and history.
  - Desktop launcher and desktop icon clicks directly invoke `terminal_window_open()`.
  - Window manager client buffer safety: `client_width` and `client_height` limits prevent out-of-bounds reads during window resizing.

### Native Shell Built-ins & Error Handling (`kernel/shell.c`, `include/shell.h`)
- **Documented Built-ins:**
  - `help`: displays documented built-in commands.
  - `echo`: arguments printing with spacing and newline.
  - `clear`: emits ANSI `\033[2J\033[H` to clear terminal screen.
  - `pwd`: displays current working directory.
  - `cd`: directory traversal (`/`, `..`, relative, absolute).
  - `ls`: directory file listing with directory markers and file sizes.
  - `cat`: reads and displays file content.
  - `touch`: creates empty file.
  - `mkdir`: creates directory.
  - `rm`: deletes file or directory.
  - `cp`: file copy via file explorer subsystem.
  - `mv`: file move/rename via file explorer subsystem.
  - `ps`: lists active processes (PID, parent PID, state, CR3 root).
  - `disks`: storage disk and partition enumeration.
  - `uname`: system version, architecture, and design language.
  - `mem`: total and free RAM statistics.
  - `env`: active environment variables.
  - `whoami`: active username (`admin`).
  - `history`: numbered list of command history.
- **Bounded Buffers & Safety:**
  - Command length bounded by `SHELL_CMD_MAX = 256U`.
  - History ring bounded by `SHELL_HISTORY_MAX = 16U`.
  - Output buffer bounded by `SHELL_OUT_MAX = 4096U`.
  - Argument parsing bounded by `SHELL_ARGS_MAX = 16U`.
  - Unknown commands return exit code 127 and display `dsh: command not found: <cmd>`.
  - Missing arguments or invalid paths return error exit code 1 with usage instructions.

### Test Harness & Validation
- `shell_self_test()`: Echo, clear screen escape, whoami, history accumulation, process list (`ps`), working directory (`pwd`), uname, environment variables, missing argument rejections (`cat`, `cp`, `mv`), 220-char long-line execution, and unknown command code 127.
- `terminal_self_test()`: Monospace grid geometry (80x25), 640x400 buffer, transparent background verification, 40-line scrollback push accumulation, viewport scrolling, `Shift+Up`/`Shift+Down` key navigation, ANSI clear screen sequence, interactive command execution (`echo test_d4`), `KEY_UP` history recall, `KEY_DOWN` history dismissal, 300-character input bounds clamping at `SHELL_CMD_MAX - 1`, and Window Manager window binding with key event dispatch.

---

## 7. Milestone D5 — Files and Disks Applications

### Files Application GUI (`kernel/file_explorer.c`, `include/file_explorer.h`)
- **Desktop Window Integration (`EXPLORER_WIDTH = 560U`, `EXPLORER_HEIGHT = 360U`):**
  - Window title `"Files"`, resizable, movable, with emerald liquid-neomorphic titlebar chrome.
  - Client render buffer: statically allocated `uint32_t explorer_render_buf[560 * 360]` ensuring zero dynamic heap allocations in hot drawing paths.
  - Window manager client dimension constraints: `client_width = 560`, `client_height = 360`.
  - Window lifecycle callbacks: `on_click` dispatched to `file_explorer_handle_click`, `on_close` safely decoupling the window reference.
- **Header Toolbar & Path Breadcrumb Navigation:**
  - `[..]` Parent directory button: ascends directory tree; automatically disabled/dimmed when at root `/`.
  - `[R]` Refresh button: reloads active directory listing from VFS.
  - Frosted glass path breadcrumb box: displays crisp active directory path (`Path: /`, `Path: /tmp`, etc.).
- **VFS Directory Enumeration & Interactive Selection:**
  - Table column header row: `TYPE`, `NAME`, `SIZE`, `STATUS`.
  - Directory items rendered with neon emerald `[DIR]` badge, file items with crisp off-white `[FILE]` badge.
  - Human-readable file size formatting (`B`, `KB`, `MB`).
  - Interactive selection: mouse click highlights item with emerald primary fill and glowing accent border.
  - Interactive directory entry: clicking already-selected `[DIR]` navigates directly into the child subdirectory.
  - Footer status bar: displays total item count, cumulative size, and currently selected item name.
- **Robust Path Validation & Safety Gates:**
  - `validate_explorer_path()`: strictly rejects NULL, empty string, paths not starting with `/`, paths exceeding `VFS_MAX_PATH`, and non-printable control characters.

### Disks Application GUI & Storage Safety Gates (`kernel/storage_manager.c`, `include/storage_manager.h`, `kernel/partition.c`)
- **Desktop Window Integration (`DISKS_WIDTH = 580U`, `DISKS_HEIGHT = 360U`):**
  - Window title `"Disks"`, resizable, movable, with emerald liquid-neomorphic titlebar chrome.
  - Client render buffer: statically allocated `uint32_t disks_render_buf[580 * 360]` in kernel memory.
  - Window manager callbacks: `on_click` dispatched to `disks_handle_click`, `on_close` safely resetting pointer.
- **Physical Device & Partition Inspection:**
  - Top Storage Safety Banner: amber frosted glass badge displaying `[RO] Storage Safety Gate Active: Host Disks Write-Protected`.
  - Two-pane layout:
    - Left pane: Discovered physical block devices (`/dev/vda`, `/dev/sda`, etc.) with model, capacity, and `[RO]` tag. Selected disk rendered with neon accent highlight.
    - Right pane: In-depth hardware telemetry and partition slices:
      - Device path, drive model, interface type (SATA/NVMe/VirtIO/Optical), partition scheme (GPT/MBR/RAW), formatted capacity, sector count.
      - Operational status: `[READ-ONLY PROTECTION ACTIVE]`.
      - Partition slice table: `SLICE`, `START LBA`, `SECTORS`, `FS` (FAT32, ISO9660, RAW), and mount status (`/mnt/...` vs `[Unmounted]`).
  - Interactive selection: clicking a drive in the left pane updates detailed inspection telemetry in the right pane.
- **Multi-Layer Storage Safety Gates:**
  - Initial Disks GUI contains zero write, format, or repartition controls (strictly read-only inspector).
  - Storage Manager API gates:
    - `storage_create_partition()`: strictly rejects operations if `disk->read_only` or `desc->read_only` with `STORAGE_ERR_READ_ONLY` (`-2`); strictly rejects out-of-range sector counts with `STORAGE_ERR_BOUNDS` (`-3`).
    - `storage_delete_partition()`: strictly rejects operations if `disk->read_only` or `desc->read_only` with `STORAGE_ERR_READ_ONLY` (`-2`).
    - `storage_format_partition()`: strictly rejects operations if partition device is read-only with `STORAGE_ERR_READ_ONLY` (`-2`).
  - Partition Driver layer gates:
    - `partition_validate_safe_operation()`: immediately returns `false` if `disk->read_only` is true.
    - `partition_create_mbr()` and `partition_delete()`: reject if `disk->read_only`.
    - `partition_format_fat32()`: rejects if `part_dev->read_only`.
    - `partition_scan_mbr()`: rejects malformed partition entries with start LBA or ending sectors exceeding `disk->total_blocks`.

### Desktop Shell Integration (`kernel/desktop.c`)
- Application Launcher menu:
  - Item 2: Files (`file_explorer_window_open()`).
  - Item 3: Disks (`disks_window_open()`).
- Desktop icons:
  - "This PC" (`y in [28, 72)`) opens Disks application (`disks_window_open()`).
  - "Files" (`y in [72, 124)`) opens File Explorer application (`file_explorer_window_open()`).

### Test Harness & Validation
- `explorer_self_test()`:
  - VFS directory enumeration on root `/`.
  - Malformed path rejection: NULL path, missing leading slash, empty path, non-printable characters, oversized path.
  - File operations: creation, search, copy, and deletion.
  - GUI window integration: creation, client buffer attachment (560x360), root directory path verification, directory navigation into `/tmp`, parent navigation back to `/`, item selection click simulation, toolbar click simulation, and safe window destruction.
- `storage_manager_self_test()`:
  - Physical disk discovery and partition query.
  - Sector bounds safety gate: verifying request with out-of-range sector count returns `STORAGE_ERR_BOUNDS`.
  - Read-only safety gates: verifying `storage_create_partition`, `storage_delete_partition`, and `storage_format_partition` strictly fail with `STORAGE_ERR_READ_ONLY` when `read_only = true`.
  - Partition lifecycle: creation and deletion on disposable RAM disk.
  - GUI window integration: creation, client buffer attachment (580x360), left-pane click selection, and safe window destruction.
