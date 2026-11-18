# DAWN-OS — Code Review & Remediation Report

- **Generated:** 2026-11-18
- **Review scope:** all commits from `1629a1f3e777f6113a112cecfddcf346df48a358` (baseline referenced in the development prompt) through `HEAD` = `951912e` (16 commits, 95 files, ~13,961 insertions).
- **Files reviewed:** `kernel/*.c`, `include/*.h`, `user/*`, `Makefile`, `README.md`, `docs/PHASE_*_STATUS.md`.
- **Method:** static review of full file contents plus `git diff`. No compilation, QEMU, or test suite was executed (project rule 9). Nothing in this report is execution-verified.

---

## 1. Executive summary

| Priority | Count | Meaning |
|---|---|---|
| P0 — Build blockers | 8 groups | The kernel cannot compile/link in any configuration as committed. |
| P1 — High | 5 | Boot failures, memory corruption, broken SYS_EXEC, unbounded recursion. |
| P2 — Medium | 11 | Protocol errors, OOB reads, input/UI defects, resource exhaustion. |
| P3 — Low | 6 | Panic-path overflow, weak credentials, ignored errors, naming collisions. |

The dominant cause of P0 is **API drift**: multiple new files (`settings.c`, `installer.c`, `shell.c`, `storage_manager.c`, `task_manager.c`, `power.c`, `main.c`) were written against function names, argument orders, types, and constants that do not exist in this tree. Because `Makefile:18` uses `-Werror`, these are hard errors.

**Consequence:** none of the self-tests added across Phases C–S are reachable from a committed build configuration. The phase docs accurately describe *source-level* tests; they do not claim execution, so there is no false test claim to retract — but the code must build before Phase T can mean anything.

---

## 2. P0 — Build blockers (fix before any other work)

### P0-1. Nonexistent graphics / window-manager API

**Files:** `kernel/settings.c:330-400,467`; `kernel/power.c:148-151`

Used but nonexistent: `gfx_surface_t`, `.pixels`, `gfx_fill_rect()`, `gfx_draw_rect()`, `GFX_COLOR_WINDOW_BG`, `GFX_COLOR_PANEL_DARK`, `GFX_COLOR_EMERALD_MUTED/PRIMARY/NEON`, `GFX_COLOR_TEXT_PRIMARY/MUTED`.

Actual API (`include/gfx.h:40-59`):

```c
struct gfx_surface { uint32_t *buffer; uint32_t width, height, pitch; }; /* pitch in BYTES */
void gfx_draw_rect_filled(struct gfx_surface *, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_rect_outline(struct gfx_surface *, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
struct gfx_surface *gfx_get_framebuffer(void);
```

`settings.c:467` also calls `wm_create_window(120, 80, 560, 380, "Settings", buffer)`; the real signature is
`struct window *wm_create_window(const char *title, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t flags)`.

**Fix approach:**
1. Replace `gfx_surface_t` with `struct gfx_surface`; `.pixels` → `.buffer`; `.pitch = 560` → `.pitch = 560 * sizeof(uint32_t)`.
2. `gfx_fill_rect` → `gfx_draw_rect_filled`; `gfx_draw_rect` → `gfx_draw_rect_outline`.
3. Map the invented color names to existing `GFX_COLOR_*` constants in `include/gfx.h` (or add the missing aliases to `gfx.h` once, then use them everywhere).
4. Change `settings_window_id` from an integer to `struct window *`, or store `win->id` from the corrected call.
5. Do the same mechanical port in `power.c:148-151`.

### P0-2. `vfs_read` arguments swapped in settings persistence

**File:** `kernel/settings.c:289`

```c
int bytes_read = vfs_read(node, 0, file_buf, sizeof(file_buf) - 1);   // wrong
```

Signature (`include/vfs.h:84`): `int64_t vfs_read(struct vfs_node*, uint64_t offset, size_t size, void *buffer)`.

**Scenario:** if `/boot/system.cfg` exists, the stack pointer is passed as a ~48-bit size and `0x3FF` is used as the destination address → page fault / memory corruption at boot. Also fails `-Wconversion -Werror`.

**Fix:** `vfs_read(node, 0, sizeof(file_buf) - 1, file_buf);`
Also audit `settings.c:310` (`vfs_write`) for the same ordering.

### P0-3. Undefined `VFS_MODE_FILE` and 2-arg `vfs_create`

**File:** `kernel/settings.c:305` (`vfs_create(path, VFS_MODE_FILE)`); also `kernel/installer.c:153-158,169,189-191,242,249,267-269` (`VFS_MODE_DIR`/`VFS_MODE_FILE`).

`vfs_create` is declared `int vfs_create(const char*, uint32_t mode, struct vfs_node **out_node)` (`include/vfs.h:88`). No `VFS_MODE_*` macros exist.

**Fix:** use `0644`/`0755` and pass a `struct vfs_node **` (or `NULL` if the caller does not need the node). Optionally add named constants to `vfs.h` and use them consistently — but add them to the header first, not in callers.

### P0-4. Installer uses a fictional storage/VFS API

**File:** `kernel/installer.c:70-71,111-127,140,153-158,169,189-191,242,249,267-269`

Nonexistent: `storage_get_disk()`, `storage_create_partition(disk_id, ptype, start_lba, count, PART_TYPE_EFI_SYSTEM)`, `storage_format_partition(disk_id, 0, label)`, `PARTITION_TABLE_GPT/MBR`, `VFS_MODE_*`.

Real API:
- `const struct storage_disk_desc *storage_get_disk_info(size_t index)` (`include/storage_manager.h`)
- `int storage_create_partition(const char *disk_name, uint64_t size_blocks, uint8_t mbr_type, char *out_slice_name)` (`include/partition.h`)
- `int storage_format_partition(const char *slice_name, const char *fs_type, const char *label)`
- Schemes/types: `PARTITION_SCHEME_MBR/GPT`, `MBR_TYPE_*`

**Fix approach:** rewrite `installer_run_full()` to (a) enumerate disks with `storage_get_disk_info`, (b) call the 4-arg `storage_create_partition` and capture the returned slice name, (c) format that slice by name. This also fixes P2-11 (wrong partition formatted).

### P0-5. Shell calls undeclared VFS wrappers

**File:** `kernel/shell.c:271,282,308`

- `vfs_open`, `vfs_close` are not declared globally in `include/vfs.h` (only the ops function-pointer typedefs).
- `vfs_mkdir(args[1], 0755)` passes 2 of 3 required arguments.

**Fix:** either add global wrappers to `vfs.c`/`vfs.h`, or replace with `vfs_resolve_path()` + `node->ops.open(node, VFS_FLAG_READ)` and pass a `struct vfs_node **` to `vfs_mkdir`. Add a `vfs_close` wrapper that calls `node->ops.close`.

### P0-6. Storage manager calls undefined `fat32_mount` and 2-arg `vfs_mkdir`

**File:** `kernel/storage_manager.c:234,236`

```c
(void)vfs_mkdir(mnt_path, 0755);
if (fat32_mount(bdev, &fat_root)) { ... }
```

`fat32_mount` is defined nowhere; only `fat32_mount_image(void*, size_t)` exists (`include/fat32.h:63`).

**Fix:**
1. `vfs_mkdir(mnt_path, 0755, NULL);`
2. Implement `bool fat32_mount(struct block_device *bdev, struct vfs_node **out_root)` that reads the FAT boot region through `block_dev_read()` into a buffer and calls `fat32_mount_image()`; declare it in `include/fat32.h`. Without this, automount cannot work even after the build is fixed.

### P0-7. Undefined process-state enum constants in Task Manager

**File:** `kernel/task_manager.c:49,59,69,84,141-144`

`PROCESS_RUNNING`, `PROCESS_READY`, `PROCESS_EXITED`, `PROCESS_BLOCKED` do not exist. Real enum: `PROCESS_STATE_FREE/CREATED/READY/EXITED` (`include/process.h:30-35`).

**Fix:** use `PROCESS_STATE_*`. For RUNNING/BLOCKED UI states, either extend `enum process_state` in `process.h` and maintain it in the scheduler, or drop those cases from the UI. Do **not** invent display-only states that claim kernel state the scheduler does not track.

### P0-8. Production build references a test-only function; test build double-spawns init

**Files:** `kernel/main.c:1325-1336`; `kernel/elf_loader.c:413-717`; `include/elf_loader.h:38`

`elf64_build_init_image()` is defined only under `#ifdef DAWN_KERNEL_TEST` but is declared unconditionally and called from the non-test path at `main.c:1331`. Normal `make kernel` fails to link.

Additionally, the final init block at `main.c:1325-1336` is **outside** the `#ifdef DAWN_KERNEL_TEST` block that ends at line 1323. On non-QEMU hardware `qemu_debug_exit()` is a no-op, so test builds spawn init twice, leaving a READY thread bound to the first, already-reaped PID.

**Fix:** move `elf64_build_init_image()` out of the test guard in `elf_loader.c` (it is production runtime data, not a test helper). Then decide the intended control flow explicitly:
- If the final block is meant for production only → wrap it in `#ifndef DAWN_KERNEL_TEST`.
- If both builds should launch init once → remove the duplicate block inside the test section.

---

## 3. P1 — High-severity correctness / security

### P1-1. xHCI dereferences a raw physical BAR under 4-level paging

**File:** `kernel/xhci.c:35-45`

```c
uint64_t mmio_base = bar0 & ~0x0FU;         /* PHYSICAL */
ctrl->cap = (struct xhci_cap_regs *)mmio_base;
ctrl->op  = (struct xhci_op_regs *)(mmio_base + ctrl->cap->caplength);  /* deref -> fault */
```

Physical addresses are not identity-mapped; `apic.c:223-225` calls `vm_map_mmio_page()` before touching MMIO.

**Trigger:** any machine with an xHCI controller (real hardware or QEMU `-device qemu-xhci`) → page fault in `input_initialize()` during boot.

**Fix:** call `vm_map_mmio_page(phys, &virt)` (or add the runtime HHDM offset), map the full BAR size, check the result, and store the virtual base in `ctrl->cap`/`ctrl->op`. Propagate failure instead of unconditionally setting `ctrl->active = true`. Apply the same audit to `ahci.c`/`nvme.c`, which use the same pattern.

### P1-2. ISO9660 name formatting overflows a 128-byte buffer

**File:** `kernel/iso9660.c:39-52`, called at `:96` and `:125`

`iso_format_name()` copies `raw_len` bytes with no bound tied to the destination. `raw_len` is the on-disk `uint8_t name_len` (up to 255); callers pass `char[VFS_MAX_NAME]` (128).

**Trigger:** crafted ISO with `name_len = 200` and no `;`, then `readdir`/`resolve_path` → 200 bytes into a 128-byte stack/struct buffer.

**Fix:** pass the output capacity into `iso_format_name()`, clamp `len` to `min(len, out_cap - 1)`, validate `name_len <= d->length - offsetof(name)`, and bounds-check `(extent * block_size + dir_size) <= fs->size` before dereferencing records (also covers `:87,117`).

### P1-3. `ramfs_write` integer overflow → arbitrary kernel write

**File:** `kernel/vfs.c:75-87`

```c
if (offset + size > RAMFS_MAX_FILE_SIZE) return -1;   /* wraps */
...
data->buffer[offset + i] = src[i];
```

**Trigger:** `vfs_write(file, 0xFFFFFFFFFFFFFFF0ULL, 32, buf)` — wrapped sum is 16, guard passes, wild write at `buffer[0xFFFF...F0 + i]`.

**Fix:**

```c
if (offset > RAMFS_MAX_FILE_SIZE || size > RAMFS_MAX_FILE_SIZE - offset) return -1;
size_t new_size = (size_t)(offset + size);
```

### P1-4. SYS_EXEC always terminates the calling process

**Files:** `kernel/scheduler.c:615-618`; `kernel/elf_loader.c:117-119`; `kernel/process.c:483-495`

`elf64_load_process()` rejects any process whose state is not `PROCESS_STATE_CREATED` and requires an empty page checkpoint. A running user process is `READY`, so the call at `scheduler.c:616` always fails and falls into `scheduler_user_exit(frame)`. Separately, `process_user_memory_rollback(pid, 0U)` at line 615 requires `CREATED` (so it silently fails today) and would unmap the pages holding `image_ptr` if it did succeed.

**Fix approach (requires a small redesign):**
1. Copy the incoming ELF image into a kernel-owned buffer **before** touching the current address space (`vm_user_range_valid` + `vm_copy_from_user`).
2. Add a dedicated `process_replace_image(pid, kernel_image, size)` that: switches the process record to a transitional `CREATED` state, rolls back old user pages, loads the new image with `elf64_load_process()`, then returns it to `READY`.
3. Only then rewrite `frame->rip`/`user_rsp` and zero `frame->rax` on success. On failure return `-EINVAL`/`-EFAULT` without exiting.
4. Do not reuse `process_user_memory_rollback(pid, 0)` from exec until step 1 is in place.

### P1-5. Recursive service dependency cycle

**File:** `kernel/service_manager.c:72-84`

`SERVICE_STARTING` is assigned only *after* the dependency is resolved, so A→B→A is never observed.

**Trigger:** register two units with mutually referencing `requires`, then `service_start("A")` or `service_start_all()` → unbounded recursion → stack exhaustion.

**Fix:**

```c
if (service_registry[i].state == SERVICE_STARTING) return false;   /* cycle */
service_registry[i].state = SERVICE_STARTING;                      /* BEFORE recursion */
if (requires[0] && service_get_state(requires) != SERVICE_RUNNING &&
    !service_start(requires)) { state = SERVICE_FAILED; return false; }
...
```

---

## 4. P2 — Medium severity

| # | Location | Issue | Fix approach |
|---|---|---|---|
| 1 | `net.c:200-209` | UDP `ntohs(udp->length) - 8` underflows for `length < 8`; copies 2048 bytes past packet. | Validate `ulen >= 8`, `avail = len - (14 + ihl + 8)`, `payload_len <= avail`; also check IHL ≥ 5. |
| 2 | `net.c:157-191` | ICMP reply length from full frame including Ethernet padding, while IP `total_length` stays smaller → checksum mismatch on small pings. | `ip_total = ntohs(ip->total_length); if (ip_total < ihl) return -1; icmp_data_len = ip_total - ihl;` and verify `len >= 14 + ip_total`. |
| 3 | `block_dev.c:112,120`; `partition.c:52-63,216-221` | `lba + count` wraps, bypassing bounds checks. | `if (lba >= total || count > total - lba) return -1;` everywhere, including slice callbacks. |
| 4 | `ps2.c:141-147` | Clears translation bit but decodes Set-1 scancodes; no `0xF0 0x01` sent. | Either keep `0x40` set, or command Set 1 explicitly before clearing it — not both. |
| 5 | `input.c:59-70` | Both pollers read the single 8042 output buffer; key/mouse bytes are stolen from each other. | Single reader that dispatches by source, or separate IRQ1/IRQ12 handlers draining their own device. |
| 6 | `terminal.c:227-230`, `task_manager.c:246-249` with `window_manager.c:288-295` | Window height uses `+4` border but `ch` subtracts only `2`; 2 rows (5120 B for terminal) read past `render_buffer` every composite. | Use `+ WM_BORDER_WIDTH` in the height, or expose buffer height and clamp `ch`. |
| 7 | `fat32.c:212-215`, `iso9660.c:127-130` | `finddir` node pools are monotonic; after 16 lookups path resolution always fails. | Free-list/recycle slots; invalidate on unmount; tie `file_info` storage to the node slot. |
| 8 | `file_explorer.c:134-143` | `vfs_read() <= 0` breaks then returns `true`; move then deletes source → data loss. | `if (bytes_read < 0) return false; if (bytes_read == 0) return false;` require final `offset == src->size`. |
| 9 | `gfx.c:239-246` | Unsigned `r1 - r0` underflow in darkening gradients → garbage colors. | Signed arithmetic with clamping: `int32_t dr = (int32_t)r1 - (int32_t)r0; cr = (uint32_t)(r0 + dr*r/h) & 0xFF;`. |
| 10 | `desktop.c:117-131,170-183` | Every taskbar tab calls `wm_get_top_window()`; all tabs show/act on the same window. | Add `wm_get_window_at_z(size_t index)` returning `z_order[index]`; use it in both loops. |
| 11 | `installer.c:120-140` | GPT/UEFI: partition 0 is ESP but formatter always targets partition 0 → root unformatted. | Capture the slice name returned by `storage_create_partition` for the root and format that (folded into P0-4). |

---

## 5. P3 — Low severity

| # | Location | Issue | Fix approach |
|---|---|---|---|
| 1 | `power.c:106-131` | Unguarded `buf[pos++] = ' '` / `'\n'` after guarded loops; long panic reason overflows the 512-byte report. | Guard every write, or centralize with a bounded `append()` helper that always truncates. |
| 2 | `security.c:28-65,102,206-209` | Predictable salt `0xDA3E0000 + uid`, non-cryptographic hash, default `root/toor`, `dawn/dawn`. | Real KDF (PBKDF2/scrypt-equivalent) with random per-user salt; force password change on first login. |
| 3 | `advanced.c:225` | `mmap_file()` ignores `vfs_read()` return → returns buffer on failure; `munmap_file()` ignores write-back. | Check returns; return NULL/false on failure. Also note `shm_map`/`mmap_file` return kernel/HHDM addresses, so "shared memory" is not actually mapped into user address spaces — document or implement real user mappings. |
| 4 | `partition.c:241-251` | Failed LBA-0 read ignored, then zeroed sector written → other MBR entries destroyed. | `if (block_dev_read(...) != 0) return -1;` |
| 5 | `service_manager.c:91-102` | Stopping a dependency leaves dependents RUNNING; they are never restarted. | On stop, stop/mark-FAILED all units whose `requires` names the stopped service. |
| 6 | `partition.c:38-49`, `storage_manager.c:41-52` | `make_slice_name()` uses `'0' + (index % 10)`; indices 1 and 11 collide. | Encode multi-digit indices (`snprintf`-style) or widen the name field. |

---

## 6. Recommended remediation order

1. **P0-1 … P0-8** — make the kernel build. Do these first; everything else is unverifiable until then. Work file-by-file against the real headers, not the phase docs (`docs/PHASE_D_STATUS.md` describes `vfs_open`/`vfs_mkdir(path, mode)` APIs that were never implemented).
2. **P1-1** — xHCI MMIO mapping (boot-blocking on common hardware).
3. **P1-2, P1-3** — memory corruption reachable from untrusted filesystem/image data and syscall arguments.
4. **P1-4, P1-5** — EXEC redesign and service cycle detection.
5. **P2-1, P2-2, P2-3** — network/storage arithmetic and bounds.
6. Remaining P2, then P3.
7. Only then enter Phase T: compile, run static analysis, execute self-tests, and record actual evidence.

### Suggested verification once fixes land

- Build both `make kernel` and `make kernel-test`; this alone catches all P0 items.
- Boot test image in QEMU **with** `qemu-xhci` attached to exercise P1-1.
- Add targeted negative self-tests: malformed ISO name length, wrapped `vfs_write` offset, UDP `length < 8`, cyclic service dependencies, `SYS_EXEC` success path.
- Keep rule 11 intact: record "Passed" only with execution evidence (serial output / exit code).

---

## 7. Files with no high-confidence issues found

`kernel/device.c`, `kernel/pci.c`, `kernel/usb_hid.c`, `include/xhci.h` (register offsets verified against spec), `kernel/input.c` ring buffer internals, `kernel/audio.c`, `kernel/vfs_cache.c`, `kernel/elf_loader.c` image validation/page math, `kernel/localization.c`, `kernel/app_runtime.c`, `user/crt0.S` (stack alignment and argv/envp math), `user/libdawn.c`, `user/include/dawn.h` (syscall numbers/registers), and most headers.

Caveat: several of these are unreachable until the P0 build blockers are fixed, so "no issues found" means static inspection only.

---

## 8. Notes

- This report reviews the diff from `1629a1f3e` to `951912e`. It does not re-review earlier Phase A/B code except where the new code calls into it.
- No files were modified and no commits were created while producing this report.
- Severity reflects real-world impact: P0 means the tree cannot produce a bootable image, P1 means boot failure or memory corruption/security exposure on a realistic input, P2 means incorrect behavior under specific inputs, P3 means hardening/robustness gaps.
