# DAWN-OS — Compiled Userspace Applications

This branch (`userland`) contains the compiled Ring-3 ELF binaries installed to `/bin/` on the DAWN-OS filesystem.

Each binary is a freestanding 64-bit ELF program targeting DAWN Syscall ABI v1.

---

## Build Metadata

- **Source Commit:** [`b331bef`](https://github.com/Pavithran26/DAWN-OS/commit/b331bef8d30093f529be4312aa1f4b04a1b07e87)
- **Compiler:** `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45125180689)

---

## Available Applications

| Binary | Description |
|---|---|
| `init.elf` | Native userland init process (PID 1) |
| `sh.elf` | Native POSIX command shell (dsh) |
| `echo.elf` | Print text to standard output |
| `cat.elf` | Concatenate and display files |
| `ls.elf` | List directory contents |
| `grep.elf` | Search pattern in files or stdin |
| `nproc.elf` | Print number of available CPU cores (SMP active) |
| `free.elf` | Display free and used physical memory |
| `whoami.elf` | Print effective user name |
| `sleep.elf` | Pause execution for N seconds |
| `pwd.elf` | Print current working directory |
| `mkdir.elf` | Create directories |
| `rm.elf` | Remove files |
| `touch.elf` | Create empty files or update timestamps |
| `cp.elf` | Copy files |
| `head.elf` | Output first N lines of files |
| `tail.elf` | Output last N lines of files |
| `wc.elf` | Word, line, and byte counter |
| `date.elf` | Print system UTC date and time |
| `uptime.elf` | Show system uptime and active CPUs |
| `uname.elf` | Print operating system name and release |
| `clear.elf` | Clear terminal display |
| `kill.elf` | Send signal to terminate processes |
| `fetch.elf` | System information fetch with emerald logo |
| `ifconfig.elf` | View network interface configurations |
| `ping.elf` | Send ICMP echo requests |
| `gui_demo.elf` | Direct framebuffer demo client |
| `ld_dawn.elf` | Userland dynamic runtime helper |

---

## Verification

Verify integrity of all binaries:
```bash
sha256sum -c SHA256SUMS
```
