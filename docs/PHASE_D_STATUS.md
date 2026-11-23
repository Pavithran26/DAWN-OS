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
