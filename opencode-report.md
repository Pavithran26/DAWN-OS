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

---

## 9. Addendum — review of `dee5c5a..7cdf945` (OS metadata, About/Credits/Help/System Info, desktop footer, click dispatch)

- **Reviewed range:** `dee5c5a` (baseline before the metadata feature) through `HEAD` = `7cdf945` — 2 commits, 20 files, ~1.9k insertions.
- **Method:** static review of `git diff dee5c5a..HEAD`, the tracked generated header, and `config/os_metadata.json`. No compilation, QEMU boot, or test run was performed; nothing below is execution-verified.
- Sections 1–8 above review `1629a1f3e..951912e` and are unchanged. This addendum covers only the later feature range.

### 9.1 P1 — High severity

**1. The new desktop UI is unreachable in production.**
`desktop_render()` and `desktop_handle_input_event()` are called only from `desktop_self_test()` (`kernel/desktop.c:396,404,414,427`). `wm_handle_mouse()` is reached only through `desktop_handle_input_event()`, and `input_poll_hardware()` (`kernel/input.c:59`) has no callers. `main()` initializes everything, spawns init, and halts (`kernel/main.c:1371-1377`).

**Trigger:** boot the production image — no footer, launcher entry, desktop icon, or new Settings tab is rendered or clickable, so the feature is not actually integrated. `docs/OS_METADATA.md:137` acknowledges the missing loop, but `README.md:70-71,170` still claims desktop integration.

**Fix:** add a production loop (poll input → `desktop_handle_input_event()` → `desktop_render()` → present), or equivalent IRQ-driven input plus a render tick. Until then, stop claiming the feature is integrated.

**2. `LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID` is wrong, so UEFI is reported as BIOS.**
`include/limine_requests.h:47-48` uses `0x5ceba5163ea08d27, 0xb64052b7107e39da`; the published `limine-protocol` header uses `0x5ceba5163eaaf6d6, 0x0a6981610cf65fcc`. Limine matches requests by exact 128-bit ID, so this request is ignored and `response` stays `NULL`.

**Trigger:** boot under UEFI — `kernel/system_info.c:117-125` falls back to `"BIOS (Limine)"`. `system_info_self_test()` only checks `boot_mode != NULL`, so `qemu-uefi-test` cannot catch it.

**Fix:** correct the ID and assert `boot_mode == "UEFI (Limine)"` in the UEFI test path.

**3. The committed build metadata is stale.**
`include/dawn_metadata_generated.h:13-15` contains build `c0fe60a120b4...` and `dawn-c0fe60a-dirty`, while HEAD is `7cdf94567f2c...`. The header is tracked, and the `Makefile:32-33` rule depends only on `config/os_metadata.json` and `tools/generate_metadata.py`, not on Git HEAD. The header is newer than the JSON, so it never regenerates.

**Trigger:** build at HEAD without deleting the header — About and System Info report a false commit and a false `-dirty` suffix.

**Fix:** generate into `build/` as a normal build artifact, depend on `git rev-parse HEAD` / `SOURCE_DATE_EPOCH`, and stop tracking the generated file.

**4. Uptime can never advance in production.**
LAPIC timer calibration and start/stop are inside `DAWN_KERNEL_TEST` (`kernel/main.c:1009,1040-1047`), and the timer is stopped afterward. No production calibration/start exists, so `apic_local_timer_calibrated_hz()` returns 0 and `kernel/system_info.c:104` pins `uptime_seconds = 0`.

**Trigger:** boot production and open System Info — `Uptime (sec): 0` permanently, despite `README.md:70,171` claiming live LAPIC uptime.

**Fix:** calibrate and start the periodic timer on the production path, or display "Not available" when the frequency is 0.

**5. "Shut Down" does not shut down.**
`kernel/desktop.c:143-147` shows `"Power" / "System Shut Down initiated"` and returns. `power_shutdown()` exists (`include/power.h:35`, `kernel/power.c:79`) and is never called.

**Trigger:** Start menu → Shut Down — the machine keeps running and the toast stays (notifications never expire; see 9.3-5).

**Fix:** call `power_shutdown()`; if it must remain a stub, remove "initiated" from the message.

### 9.2 P2 — Medium severity

**6. New Terminal / Task Manager icons open blank duplicate windows.**
`kernel/desktop.c:205,210,215` call `wm_create_window(...)` directly, but `main.c:820,829` already run `terminal_initialize()` and `task_manager_initialize()`, which create and bind their own windows (`kernel/terminal.c:227-234`, `kernel/task_manager.c:243-250`). The icons add empty client areas on top of the real apps; the Files icon has no application window at all.

**Fix:** focus `terminal_get_instance()->win` / `task_manager_get_instance()->win`; implement or drop the Files icon until an explorer window exists.

**7. Settings cannot be reopened after closing it with the titlebar X.**
The X path calls `wm_destroy_window()` (`kernel/window_manager.c:237-239`) without clearing `settings_window_id`. Both `settings_window_open_tab()` (`kernel/settings.c:724-726`) and `settings_window_open()` (`:735-737`) short-circuit on `settings_window_id >= 0`, focus a dead ID, and return `true`.

**Trigger:** open Settings, click X, then click About OS / Help / This PC — nothing opens for the rest of the boot. `settings_self_test()` closes via `settings_window_close()`, so the X path is never exercised.

**Fix:** verify `wm_get_window_by_id((uint32_t)settings_window_id)` before focusing, or reset the ID from a close callback.

**8. A documented null license fails the metadata self-test.**
`docs/OS_METADATA.md:31` says `project.license` may be `null`. The generator emits `DAWN_META_HAS_LICENSE 0`, and `kernel/os_metadata.c:178-182` sets `view.license_name = NULL`, but `os_metadata_self_test()` rejects exactly that (`kernel/os_metadata.c:262`).

**Trigger:** set `"license": null`, run `qemu-test` — boot fails on a configuration the docs call valid.

**Fix:** accept a null license and assert the UI renders "Not available".

**9. Help advertises commands and flags that do not exist.**
`kernel/settings.c:569` lists `sysinfo` (no shell command) and `ps` (listed in `shell.c:186` but has no dispatcher); `:580` says "Boot with safe-mode flag" — no such flag exists; `:586` says "Ring 3 ELF execution & pipes" — no pipe support exists.

**Trigger:** follow the Help instructions — "command not found" or no effect. This is new user-facing content in the changed Help tab.

**Fix:** implement the features or remove the claims.

**10. Attribution and URLs are hardcoded instead of read from metadata.**
`kernel/settings.c:499` hardcodes "About DAWN-OS"; `:541` hardcodes "Developed and maintained by Pavithran"; `:516-518,546-549,572-573,601-602` embed URLs in visible labels while the clicks use `meta->...`. `meta->credits_line` is built in `kernel/os_metadata.c:230-235` but never used. `:582` labels `support_website` as "Documentation" while `documentation_url` is null.

**Trigger:** change `developer.display_name` or `repository` and rebuild — About updates, but Credits and link labels disagree with the actual destinations.

**Fix:** format labels from `meta` / `credits_line`; use `documentation_url` or omit the Documentation link.

**11. About and Help content is clipped by the client area.**
The Settings buffer is 560×380, but `wm_composite()` copies only `width-4` × `height-titlebar-4` = 556×348 (`kernel/window_manager.c:313-321`). About's copyright at y 334..349 loses its bottom rows; `settings_draw_limited(..., 50)` truncates the 58-character notice mid-word (`kernel/settings.c:529`); Help page-4 labels from x=160 run to x≈664 and lose roughly 13 characters (`:601-602`).

**Fix:** render at the actual 556×348 client size, wrap or ellipsize long labels, and move the copyright line up.

**12. Taskbar footer overlaps the tray and the window-tab strip, and intercepts clicks.**
Tray text is drawn at `tb_y+16..31` (`kernel/desktop.c:296-297`); footer text at `tb_y+30..45` (`:305`), so the bottom two rows collide. The footer starts at `width-460` and the domain at `width-172`, while the tray starts at `width-180`, so they overwrite each other horizontally. The footer hit test (`:158-167`) also runs before the window-tab test (`:175`), so a click in the lower part of an overlapping tab opens the URL instead of focusing the tab.

**Fix:** separate the footer and tray vertically; hit-test tabs before the footer.

**13. Help Prev/Next hitbox is 20 px taller than the buttons.**
Buttons are drawn at y 235..258 (`kernel/settings.c:605-608`), but the handler accepts y 215..260 (`:703`), so clicking empty space above a button changes the page.

**Fix:** use identical geometry for rendering and hit-testing.

**14. Clicks repeat while the left button is held.**
`wm_handle_mouse()` dispatches `on_click` on every packet where `left_button` is set (`kernel/window_manager.c:214-261`); PS/2 motion packets carry the current button state (`kernel/ps2.c:99-126`). Once a production input loop exists, holding the button and moving re-fires tab switches, URL notifications, and icon actions.

**Fix:** track the previous button state and dispatch on the press edge only.

### 9.3 P3 — Low severity

**15. `dawn_decimal_string()` writes past a 1-byte buffer for zero.**
`kernel/system_info.c:24-27` writes `dst[0]` and `dst[1]` without checking `capacity >= 2`. Current callers pass 24/32 bytes, but the API contract is broken.

**Fix:** return 0 when `capacity < 2`.

**16. Desktop object dependencies omit `include/settings.h`.**
`Makefile:132` and `:282` do not list the header, although `kernel/desktop.c` includes it and uses `TAB_*` constants. Changing the enum rebuilds `settings.o` but leaves `desktop.o` with stale values.

**Fix:** add the header to both rules.

**17. Non-ASCII metadata can break the generated header.**
`tools/generate_metadata.py:248-249` emits non-ASCII code points as `\xNN`. C hex escapes are greedy, so `©2026` becomes `\xa92026` (out of range / `-Werror` failure), and the bytes are Latin-1 rather than UTF-8. The 8×16 font maps anything outside 32..126 to a space anyway.

**Fix:** emit UTF-8 bytes with separated escapes, or reject non-ASCII at generation time.

**18. Documentation overstates metadata adoption.**
`docs/OS_METADATA.md:77` says the release version is "never hardcoded in the UI", but `uname` still prints `DAWN-OS 1.0.0-release` (`kernel/shell.c:331`, pre-existing). Either route it through `meta->version` or soften the claim.

**19. Notification TTL is never decremented (pre-existing mechanism, new callers).**
`kernel/desktop.c:78` sets `ticks_remaining = 300`, but nothing decrements it. The new link clicks and the fake shutdown leave permanent toasts; after four notifications, new feedback is dropped.

**Fix:** decrement in the render/tick path and clear expired entries.

### 9.4 Suggested fix order for this range

1. Production input/render loop and press-edge dispatch (9.1-1, 9.2-14).
2. EFI request ID and build-metadata generation (9.1-2, 9.1-3).
3. LAPIC timer on the production path (9.1-4).
4. Shut Down wiring, app-window wiring, and Settings lifecycle (9.1-5, 9.2-6, 9.2-7).
5. Metadata null handling, Help accuracy, hardcoded labels, and layout/hitbox fixes (9.2-8 … 9.2-13).
6. Build/regen and generator hardening (9.3-15 … 9.3-19).

### 9.5 Checked and found sound

- `gfx_put_pixel`, `gfx_blend_pixel`, `gfx_draw_char`, and `gfx_draw_line` all bounds-check, so overlong link labels clip visually instead of writing out of bounds.
- Help page wrap arithmetic (`>3` / `%4`) is correct apart from the hitbox noted in 9.2-13.
- `desktop_open_url()` validates the URL through `dawn_url_is_valid()` before acting; the URL copy path rejects truncation and invalid input.
- Settings tab hit-testing matches the drawn tab bounds after the change.
- `config/os_metadata.json` values (version, URLs, license) are internally consistent with the generated header.

*End of addendum. No files outside this report were modified, and no commits were created.*
