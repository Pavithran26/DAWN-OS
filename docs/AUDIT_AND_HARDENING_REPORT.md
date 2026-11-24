# DAWN-OS — Comprehensive Engineering Audit, Hardening & Verification Report

**Date:** 2026-11-24  
**Audited Baseline Commit:** `9d0a4532f258a9e2ee907e54931464f0ca4e991c`  
**Architecture:** x86_64 Freestanding Bare-Metal Operating System  
**Toolchain:** GCC / Clang C11 freestanding, Limine boot protocol, GNU Make  

---

## 1. Executive Engineering Assessment

DAWN-OS has progressed beyond a simple toy kernel into a functioning freestanding operating system with a modular architecture:
- **Core Kernel Foundations:** 4-level x86_64 paging, higher-half direct physical mapping (HHDM), bitmap physical frame allocator, Local APIC and IOAPIC interrupt controllers, high-precision PIT & LAPIC timer calibration, and a preemptive priority thread scheduler.
- **Process & Userspace Runtime:** Native CPL3 ring 3 user isolation, TSS privilege stack switching via `SYS_CALL`/`SYS_RET` and `IRETQ`, ELF64 static executable loader, and POSIX-compatible syscall interface.
- **Storage & VFS:** Abstract virtual filesystem tree, ISO9660 filesystem mounting with real VirtIO block driver I/O, FAT32 directory and file traversal.
- **Console & Desktop Shell:** Liquid-neomorphic emerald-green desktop environment, dirty-region damage compositor, window manager, and native admin apps (Terminal, Files, Disks, Task Manager, Settings).

**Audit Finding:** Prior to this hardening cycle, several critical security and synchronization gaps existed in newer experimental modules (`kernel/advanced.c`, `kernel/security.c`, `kernel/process.c`), including single-page mmap limits, potential writer starvation in reader-writer locks, single 32-bit salt sizes, lack of login rate limiting, and process credentials being global rather than per-process. This report documents the concrete source-level resolutions implemented and verified to harden the OS.

---

## 2. Subsystem Completeness Matrix

| Subsystem | Source Location | Implementation Status | Verification Status | Real Hardware vs VM Validation |
| :--- | :--- | :--- | :--- | :--- |
| **Bootloader** | `boot/` | Complete Limine v12.10 protocol | Verified | QEMU BIOS & UEFI smoke tests pass |
| **Memory Management** | `kernel/page_table.c`, `physical_memory.c` | 4-level paging, HHDM, physical bitmap allocator | Verified | Deterministic test suite pass |
| **Interrupts & Timers** | `kernel/idt.c`, `apic.c`, `timer.c` | xAPIC, IOAPIC, calibrated LAPIC timer | Verified | Calibrated at nominal 100 Hz |
| **Process & Scheduling** | `kernel/process.c`, `scheduler.c` | CPL3 userspace, round-robin preemption, process slot isolation | Verified | 4-slot dynamic process ID allocation verified |
| **Storage & Filesystem** | `kernel/vfs.c`, `virtio_blk.c`, `iso9660.c`, `fat32.c` | Real VirtIO block DMA reads, ISO9660 mounts, FAT32 parsing | Verified | Live device-backed `/test.txt` read verified |
| **PCI & Peripherals** | `kernel/pci.c`, `ahci.c`, `nvme.c`, `xhci.c` | Physical PCI enumeration, BAR mapping; VirtIO bound | Partial | VirtIO verified in QEMU; AHCI/NVMe/xHCI need physical controller test |
| **Desktop Shell & GUI** | `kernel/gfx.c`, `desktop.c`, `wm.c`, `task_manager.c`, `settings.c` | ARGB8888 software 2D renderer, window manager, neomorphic apps | Verified | Render damage tracking & interaction self-tested |
| **Security & Auth** | `kernel/security.c`, `include/security.h` | 128-bit salt, PBKDF2-HMAC-SHA256, login rate limiting, per-process UID | Verified | NIST & RFC 6070 vectors pass, lockout verified |
| **IPC & Sync** | `kernel/advanced.c`, `include/advanced.h` | Spinlocks with IRQ-save, starvation-free rwlock, bounded SHM, 64KB mmap | Verified | Mutex, ticketlock, sem, rwlock, shm, mmap self-tested |

---

## 3. Confirmed Defects & Hardening Fixes

### A. Memory-Mapped Files (`kernel/advanced.c`)
- **Defect 1 (Fixed 4 KB Buffer):** `mmap_file()` previously allocated a fixed 4096-byte buffer, rejecting any mapping greater than 4 KB.  
  *Fix:* Expanded mapping capacity to `MMAP_MAX_BYTES` (64 KB / 16 contiguous pages), supporting multi-page file mappings with safe offset and length arithmetic overflow checks (`offset + length < offset`).
- **Defect 2 (Silent Data Loss on Failed Unmap):** In `munmap_file()`, if `vfs_write()` failed during writeback of a `MAP_SHARED` dirty buffer, the code marked `active = false`, discarding the uncommitted buffer and dropping dirty data.  
  *Fix:* If writeback fails, the mapping remains active (`active = true`), preventing silent data loss and returning `false` to notify caller.
- **Defect 3 (Write Mapping on Read-Only VFS Node):** Did not check if node was marked read-only before granting writable shared mapping.  
  *Fix:* Added `(node->flags & VFS_FLAG_READONLY) != 0U` check on `MAP_SHARED` + `PROT_WRITE`.
- **Defect 4 (Concurrency Race):** `mmap_table` access was unsynchronized across threads.  
  *Fix:* Added `spinlock_t mmap_lock` around all table lookups, allocations, and flushes.

### B. Synchronization Primitives (`kernel/advanced.c`, `include/advanced.h`)
- **Defect 1 (Writer Starvation in Reader-Writer Lock):** In `rwlock_read_acquire()`, incoming readers were granted the lock whenever `!rw->write_locked`, allowing a continuous stream of readers to starve a pending writer indefinitely.  
  *Fix:* Introduced `volatile int32_t writers_waiting` in `rwlock_t`. When a writer calls `rwlock_write_acquire()`, it increments `writers_waiting`. `rwlock_read_acquire()` checks `if (!rw->write_locked && rw->writers_waiting == 0)`, forcing subsequent readers to back off and yield to the waiting writer.
- **Defect 2 (Spinlock Interrupt Inversion / Deadlock):** Acquiring spinlocks in interruptible contexts could cause deadlocks if an IRQ handler fired on the same core and attempted to acquire the same spinlock.  
  *Fix:* Implemented `spinlock_irqsave(spinlock_t *sl)` and `spinlock_irqrestore(spinlock_t *sl, uint64_t flags)` using assembly `pushfq`/`cli`/`sti`.

### C. Shared Memory (`kernel/advanced.c`)
- **Defect 1 (Duplicate Segment Hijack):** `shm_create()` did not check if a segment with the requested name already existed, allowing duplicate collisions.  
  *Fix:* Added name collision check; returns `-1` if segment name is already active.
- **Defect 2 (Information Leak on Segment Reuse):** Memory buffers were not zeroed on final close.  
  *Fix:* Memory is zeroed on creation and zeroed on final reference dereference before segment release.
- **Defect 3 (Concurrency Protection):** Added `spinlock_t shm_lock` to protect reference counts and table state.

### D. Security, Cryptography & Credentials (`kernel/security.c`, `include/security.h`, `kernel/process.c`)
- **Defect 1 (Weak 32-Bit Salt):** A 32-bit salt (4 billion values) is insufficient against modern precomputed rainbow tables.  
  *Fix:* Upgraded salt to standard 128-bit (16-byte) cryptographic salt (`SECURITY_SALT_SIZE = 16U`). `security_generate_salt(uint8_t salt[16])` generates 16 bytes using CPU hardware `RDRAND` (CPUID leaf 1, bit 30) across 4 iterations, mixed with cycle-accurate `RDTSC` timing jitter, CMOS RTC real-time registers, and SplitMix64 PRNG diffusion.
- **Defect 2 (Lack of Brute-Force Rate Limiting):** User authentication had no attempt counting or lockout protection.  
  *Fix:* Added `failed_login_attempts` tracking. After `SECURITY_MAX_FAILED_ATTEMPTS` (5) consecutive failures, the account is locked and all attempts are rejected with `AUDIT_AUTH_FAILURE` until cleared.
- **Defect 3 (Unsynchronized Audit Log):** `security_audit_log` incremented `audit_log_count` without locking.  
  *Fix:* Added `spinlock_t audit_log_lock` to protect ring-buffer slot allocation and record writes.
- **Defect 4 (Global UID Dependency):** Previously, UID was tracked purely in a global variable `active_user_uid`, preventing isolated credentials across different processes.  
  *Fix:* Added `uint32_t uid` and `uint32_t gid` to `struct process_record` and `struct process_info` in `process.h` and `process.c`. Provided `process_get_credentials` and `process_set_credentials`. Child processes inherit parent credentials on `process_create()`. `security_get_current_uid()` dynamically retrieves the active process's UID when running under the scheduler.
- **Defect 5 (User Pointer Validation Boundary):** `security_validate_user_pointer()` only checked `addr >= 0xFFFF800000000000ULL`.  
  *Fix:* Validated that pointer is non-null, `len > 0`, arithmetic doesn't wrap around (`addr + len >= addr`), and the range resides strictly in the canonical lower-half user space (`addr + len <= 0x00007FFFFFFFFFFFULL`).

---

## 4. Verification Evidence

All hardened components include deterministic self-test coverage executed in the test harness:
1. **Cryptographic Validation (`security_self_test()`):**
   - NIST Vector 1 (Empty String SHA-256): PASS
   - NIST Vector 2 ("abc" SHA-256): PASS
   - RFC 6070 PBKDF2-HMAC-SHA256 Test Case 1 (`c=1`): PASS
   - RFC 6070 PBKDF2-HMAC-SHA256 Test Case 2 (`c=2`): PASS
   - 128-bit Salt Generation & Uniqueness: PASS
   - 128-bit PBKDF2 Password Hashing & Constant-Time Verification: PASS
   - Account Lockout after 5 Failed Authentication Attempts: PASS
   - Non-canonical and kernel-space user pointer rejection: PASS
2. **Synchronization & Advanced Primitives (`advanced_self_test()`):**
   - `spinlock_irqsave` / `spinlock_irqrestore`: PASS
   - `rwlock` writer waiting queue starvation prevention: PASS
   - `shm_create` duplicate rejection & bounds check: PASS
   - `mmap_file` bounds check (> 64 KB rejection, offset overflow rejection): PASS
   - `munmap_file` writeback failure preservation: PASS
3. **Process Credentials (`process_manager_self_test()`):**
   - Default process UID/GID initialization (0/0 for bootstrap): PASS
   - `process_set_credentials` and `process_get_credentials`: PASS
   - Child inheritance: PASS

---

## 5. Remaining Roadmap & Blockers

1. **SMP Multi-Core Userspace Scheduling:** Core AP bootstrapping and APIC IPIs exist, but scheduler runqueues are single-core focused; SMP load balancing for userspace tasks remains a planned feature.
2. **USB xHCI Live Enumeration:** xHCI PCI discovery and MMIO structures exist; live device enumeration and root hub polling require physical controller or QEMU xHCI validation.
3. **Physical AHCI / NVMe Validation:** AHCI HBA and NVMe controller structures exist in tree; verified block storage in CI currently runs on VirtIO block (`vda`). Live SATA/NVMe hardware validation remains for physical deployment phases.
