# DAWN-OS Graphical Administration Shell — Design Plan

## Status and product boundary

**Status: planned; no graphical desktop applications are implemented yet.**

DAWN-OS remains a small, server-first x86_64 operating system. The graphical environment described here is an optional local administration shell for a VM or directly attached console. It must not be required for booting, networking, or hosting services. Headless mode remains the default and must not start a compositor, input UI, or desktop services.

This plan is a design target, not a statement of current functionality.

## Visual direction

Use a restrained **green liquid-neomorphism** visual language:

- Deep forest-green base surfaces with dark green background layers.
- Mint/emerald accents for selected controls, active states, and success indicators.
- Soft inset and raised surfaces, rounded panels, subtle translucent layers, and carefully limited glow.
- Legible typography, strong contrast, clear focus rings, and keyboard-first navigation.
- Motion should be short and optional; provide a reduced-motion setting.
- Avoid GPU-only effects and expensive continuous blur. The first compositor should be able to render on the CPU framebuffer.

The desktop must prioritize clarity and responsiveness over decoration. Avoid making status information hard to read through excessive translucency.

## Desktop shell

The initial shell should contain:

1. A compact top status bar for clock, current workspace, network state, and system status.
2. A launcher/dock for Terminal, Files, Disks, and System Monitor.
3. A simple window manager supporting focus, move, resize, minimize, maximize, and close.
4. A notification/status surface for recoverable errors and device events.
5. A shutdown/restart flow with confirmation and a safe default.

No graphical login manager is required for the first prototype. The first VM image may boot directly into a demo shell only when an explicit graphical test mode is selected.

## Applications and required foundations

### Terminal

**Depends on:** userspace, process creation, syscall ABI, console/PTY-style input/output, and a minimal shell runtime.

Initial scope:
- Text rendering, cursor, scrolling, selection, and keyboard input.
- A small documented set of shell built-ins such as `help`, `pwd`, `ls`, `cd`, `cat`, and `clear`.
- Explicit error reporting and bounded input/output buffers.
- No claim of POSIX compatibility unless implemented and tested.

Validation:
- Keyboard input and serial/console output agree for the same command results.
- Long lines, backspace, scrolling, invalid commands, and buffer limits are tested.
- A malformed command cannot corrupt kernel memory.

### File Explorer

**Depends on:** VFS/path resolution, block I/O, a documented filesystem, directory enumeration, and userspace permissions.

Initial scope:
- Browse mounted volumes and directories.
- Display file name, type, size, and metadata.
- Open supported text files and copy/move files only after safe write operations exist.
- Show mount and I/O errors instead of silently hiding them.

Validation:
- Root, empty directories, nested paths, long names, missing paths, and permission failures.
- Path traversal and malformed filesystem metadata tests.
- Read/write and recovery tests before enabling destructive operations.

### Partition Manager

**Depends on:** block-device discovery, virtio-blk or another tested storage driver, sector I/O, and GPT/MBR parsers.

Safety progression:
1. Start with read-only device and partition discovery.
2. Validate GPT primary/backup headers, CRCs, bounds, overlaps, sector sizes, and malformed tables.
3. Add mounting only after filesystem support exists.
4. Consider partition creation/deletion/formatting only after extensive block-I/O tests, explicit destructive-operation confirmation, and reliable recovery behavior.

The first UI must not format or rewrite a disk. QEMU test disks must be disposable files, never the host's real disks.

Validation:
- Empty disks, GPT, MBR, corrupt tables, out-of-range LBAs, duplicate/overlapping entries, and device removal.
- Read-only mode is enforced in the block layer, not merely hidden in the UI.

### Task Manager / System Monitor

**Depends on:** process model, scheduler accounting, timers, memory accounting, and safe process-control syscalls.

Initial scope:
- List process/thread IDs, names, state, CPU time, and memory usage where those metrics are actually available.
- Show total and available memory only after the memory manager exposes reliable counters.
- Refresh at a bounded rate; do not poll in a busy loop.
- Process termination controls come later and require permission checks and confirmation.

Validation:
- Process list consistency during process creation/exit.
- Counter overflow, stale process IDs, concurrent updates, and high process counts.
- UI monitoring overhead must be measured and kept low.

## Architecture sequence

Do not implement the applications as kernel code. Keep the kernel responsible for hardware, memory, scheduling, syscalls, and safe device interfaces. Build the UI and its applications as userspace components.

Recommended order:

1. Finish and retain the BIOS/UEFI QEMU boot tests.
2. Implement physical memory, virtual memory, exceptions, interrupts, and timer.
3. Add scheduler, process model, synchronization, and userspace/syscall boundary.
4. Add framebuffer mapping and a minimal CPU 2D renderer; add QEMU keyboard/mouse input.
5. Build a basic compositor/window manager and shell terminal.
6. Add VFS and read-only filesystem support, then the file explorer.
7. Add block storage and safe GPT/MBR inspection, then a read-only partition manager.
8. Add process/memory accounting and task manager.
9. Consider safe write/format operations only after dedicated recovery testing.

## Acceptance criteria

A graphical-shell milestone is not complete until all applicable checks pass:

- Boots in QEMU BIOS and UEFI without requiring graphics mode.
- Headless mode remains functional and has no GUI background services.
- Graphical mode has deterministic framebuffer initialization and input handling.
- Terminal, file browsing, partition inspection, and task monitoring work against DAWN-OS-native kernel/userspace interfaces—not Linux-hosted programs.
- Malformed input, filesystem metadata, partition tables, and process IDs are handled without crashes or memory corruption.
- UI interaction remains responsive under CPU and memory pressure.
- Screenshots and serial logs are retained for the tested VM configuration.
- Every application and feature is labelled implemented, partial, or planned accurately in the README.

## Current implementation boundary

The current Phase A kernel requests a framebuffer and validates the Limine framebuffer response. It does not draw a graphical desktop, manage windows, accept keyboard/mouse input, mount filesystems, enumerate partitions, or list processes. A framebuffer response only confirms that the bootloader supplied framebuffer metadata; it is not a graphical user interface.
