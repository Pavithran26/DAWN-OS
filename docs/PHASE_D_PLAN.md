# Phase D — Graphical Desktop Shell Plan

## Status

**Milestones D0 (Desktop Specification & Design System) and D1 (Safe Framebuffer & CPU 2D Renderer) are fully implemented and verified.** Input event delivery (D2), window manager (D3), and desktop applications (D4-D6) follow in sequential dependency order.

Phase D is the optional local-console desktop for DAWN-OS. It must not become a boot requirement: headless/server mode remains the default and must continue to work without graphics or input devices.

The existing [GUI shell design](GUI_SHELL_PLAN.md) defines the visual direction and application-level scope. This document turns that design into gated engineering milestones.

## Visual target

- Deep forest-green surfaces with emerald and mint accents.
- Green liquid-neomorphism: restrained raised/inset panels, soft highlights, translucent layers, and subtle glow.
- Readable typography, accessible contrast, visible keyboard focus, and clear selected/disabled/error states.
- Short optional transitions; reduced-motion mode.
- CPU framebuffer rendering first. No mandatory GPU, 3D acceleration, shader, or continuous blur dependency.
- Consistent spacing, radii, icon sizing, and window chrome across all built-in applications.

The interface should look like a coherent operating system desktop, not a web page or a collection of static mockups.

## Dependency gates

Phase D is downstream of Phase B and Phase C. A Limine framebuffer response only supplies display metadata; it does not prove that pixels can safely be written, that input works, or that userspace applications can run.

Before a persistent desktop is enabled, the kernel must have:
- A tested physical frame allocator and page-table/virtual-memory manager.
- A defined framebuffer mapping and memory-ownership policy.
- Exception/interrupt handling and a timer.
- Keyboard and pointer input paths with bounded queues and safe interrupt handling.
- Kernel threads/scheduler and a userspace process/address-space model.
- A syscall boundary and safe device interfaces.
- Storage/VFS interfaces before Files and Disks applications are connected to real data.

Do not bypass these gates by placing a desktop, filesystem, shell, or application logic inside privileged kernel code.

## Milestones

### D0 — Desktop specification and design system
- Define desktop geometry, status bar, dock/launcher, workspace model, window states, notifications, and shutdown confirmation.
- Define green design tokens for colors, spacing, radii, shadows, focus, disabled, warning, and error states.
- Define keyboard navigation and reduced-motion behavior.
- Produce deterministic layout/state specifications and test cases.

**Exit:** design tokens and interaction/state contracts are documented; no claim of runtime UI.

### D1 — Safe framebuffer and CPU 2D renderer
- Validate framebuffer address, dimensions, pitch, bits per pixel, channel masks, and all size arithmetic before use.
- Establish an explicit mapping/ownership policy; never assume a fixed physical or HHDM address.
- Implement bounded pixel, fill-rectangle, line, glyph, and clipped blit primitives.
- Keep drawing out of interrupt context; use a bounded dirty-region or frame scheduling strategy.
- Add a headless renderer test harness using a memory-backed framebuffer and deterministic output checks.

**Exit:** renderer tests pass for supported formats, clipping, overflow, invalid pitch, and out-of-bounds coordinates; VM framebuffer smoke test is captured. Until then, do not write to the framebuffer.

### D2 — Input and event delivery
- Add tested keyboard and pointer drivers for the initial QEMU device configuration.
- Normalize input into bounded events with explicit overflow behavior.
- Separate interrupt-side capture from UI-side event processing.
- Support keyboard-first operation and focus indication.

**Exit:** key press/release, modifiers, pointer movement/buttons, queue overflow, and absent-device tests pass without blocking or corrupting kernel state.

### D3 — Compositor and window manager
- Implement a single-process or otherwise explicitly bounded compositor only after the process and scheduling model exists.
- Add desktop background, status bar, dock, focus, move, resize, minimize, maximize, close, and workspace state.
- Use damage tracking and bounded redraw; avoid unbounded allocations and busy loops.
- Preserve headless boot and provide an explicit opt-in graphical test mode.

**Exit:** multiple test windows can be manipulated reliably; redraw and input remain responsive under load; headless BIOS/UEFI tests remain green.

### D4 — Terminal application
- Run as a userspace application using documented console/input interfaces.
- Start with a small shell and documented built-ins; do not claim POSIX compatibility.
- Bound command length, history, output buffers, and scrollback.
- Add clear error handling for unknown commands and unavailable services.

**Exit:** interactive command tests, long-line tests, invalid input, scrolling, and buffer-boundary tests pass without kernel memory corruption.

### D5 — Files and Disks applications
- Files depends on VFS, path resolution, directory enumeration, permissions, and a tested filesystem.
- Disks begins as strictly read-only device and GPT/MBR inspection.
- Enforce read-only behavior in the block/storage layer, not only by hiding UI controls.
- Never format or repartition a host disk. Destructive features require disposable test images, explicit confirmation, bounds checks, and recovery tests.

**Exit:** malformed paths/filesystem metadata/partition tables and out-of-range sectors are rejected; initial disk UI cannot write.

### D6 — System Monitor / Task Manager
- Display only metrics exposed by reliable scheduler and memory accounting interfaces.
- Refresh at a bounded rate and measure monitoring overhead.
- Process termination controls come only after safe process-control syscalls and permission checks.

**Exit:** process lifecycle races, stale IDs, counter overflow, high process counts, and monitor overhead are tested.

## Test matrix

Every milestone must retain serial logs and relevant VM evidence for:
- QEMU BIOS and UEFI.
- Headless boot with no graphics/input device.
- Graphical mode with the documented virtual framebuffer and input devices.
- Invalid framebuffer metadata, malformed input, and bounded-queue overflow.
- Low-memory and CPU-pressure scenarios where supported.
- Repeated launch/close and shutdown/restart flows.

A successful build or a framebuffer pointer alone is not a graphical-shell pass. Physical hardware and Secure Boot remain separate validation targets.

## Non-goals for the first desktop

- Mandatory GUI boot or graphical login manager.
- GPU-only compositor or continuous blur effects.
- POSIX compatibility claims.
- Writable filesystem or partition formatting before storage safety gates pass.
- Kernel-resident application code.
- Advertising Terminal, Files, Disks, or Task Manager as implemented before their acceptance tests pass.

## Current next step

Phase B, Phase C, and Phase D milestones D0, D1, D2, D3, and D4 are complete and verified across QEMU BIOS and UEFI. Proceed to Milestone D5 (Files and Disks applications: directory enumeration, read-only disk/partition inspection, and storage safety gates).
