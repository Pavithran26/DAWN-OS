# DAWN-OS — Production Kernel ELF Binaries

This branch (`kernel`) hosts production 64-bit ELF kernel executables (`dawn-kernel.elf`) compiled with strict `-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef` standards.

---

## Kernel Specifications

- **Target Architecture:** `x86_64` (AMD64)
- **Format:** ELF64 executable, statically linked (`-z max-page-size=0x1000`)
- **Virtual Base Address:** `-2 GiB` top of 64-bit address space (`0xFFFFFFFF80000000`)
- **Boot Protocol:** Limine Boot Protocol v12
- **Source Commit:** [`9bf1ca3`](https://github.com/Pavithran26/DAWN-OS/commit/9bf1ca3436aea9ebf7d71b552deb57701a12a86f)
- **Compiler:** `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45121979973)

---

## Files

- `latest/dawn-kernel.elf` — Production kernel binary.
- `latest/dawn-kernel-headers.txt` — Section and program headers (`readelf`).
- `manifest.json` — Build and compiler metadata.
- `SHA256SUMS` — SHA-256 verification sums.
