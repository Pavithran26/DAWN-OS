# DAWN-OS — Production Kernel ELF Binaries

This branch (`kernel`) hosts production 64-bit ELF kernel executables (`dawn-kernel.elf`) compiled with strict `-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef` standards.

---

## Kernel Specifications

- **Target Architecture:** `x86_64` (AMD64)
- **Format:** ELF64 executable, statically linked (`-z max-page-size=0x1000`)
- **Virtual Base Address:** `-2 GiB` top of 64-bit address space (`0xFFFFFFFF80000000`)
- **Boot Protocol:** Limine Boot Protocol v12
- **Source Commit:** [`94d4b3e`](https://github.com/Pavithran26/DAWN-OS/commit/94d4b3e03341203f8da68400017af5e8903baafd)
- **Compiler:** `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45176616827)

---

## Files

- `latest/dawn-kernel.elf` — Production kernel binary.
- `latest/dawn-kernel-headers.txt` — Section and program headers (`readelf`).
- `manifest.json` — Build and compiler metadata.
- `SHA256SUMS` — SHA-256 verification sums.
