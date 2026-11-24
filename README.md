# DAWN-OS — QEMU Bootable Hybrid ISO Artifacts

This branch (`qemu`) hosts genuine bootable hybrid ISO images for **DAWN-OS**, an independent x86_64 hobby operating system kernel with Limine v12.10.0 bootloader, neo-liquid-glass GUI, and Ring-3 userspace.

The ISO image is dual-bootable under both **legacy BIOS** (SeaBIOS) and **modern 64-bit UEFI** (OVMF).

---

## Latest Build Information

- **Source Commit:** [`1bff56c`](https://github.com/Pavithran26/DAWN-OS/commit/1bff56c61f214cc6304fa3619df9a91dc4927f38)
- **Full SHA:** `1bff56c61f214cc6304fa3619df9a91dc4927f38`
- **Build Timestamp:** `2026-11-24T05:39:52Z`
- **Compiler:** `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- **CI Workflow Run:** [GitHub Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45334064499)
- **Validation Status:** PASSED (BIOS & UEFI: 0 FAIL, QEMU exit code 33)

---

## Artifact Catalog

| File | Type | Description | Download |
|---|---|---|---|
| `latest/dawn-os.iso` | Production ISO | Standard bootable hybrid ISO with desktop compositor and userspace apps | [Download](latest/dawn-os.iso) |
| `latest/dawn-os-test.iso` | Self-Test ISO | Kernel built with complete test harness and diagnostic reporting | [Download](latest/dawn-os-test.iso) |
| `manifest.json` | Metadata | Complete build manifest with SHA-256 digests and environment info | [View](manifest.json) |
| `SHA256SUMS` | Checksums | Cryptographic checksums for integrity verification | [View](SHA256SUMS) |

---

## Verification

Verify image integrity with `sha256sum`:

```bash
sha256sum -c SHA256SUMS
```

---

## How to Boot with QEMU

### 1. Legacy BIOS Boot (SeaBIOS)
```bash
qemu-system-x86_64 -M q35 -m 256M -smp 2 \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -cdrom dawn-os.iso -serial stdio
```

### 2. Modern 64-bit UEFI Boot (OVMF)
```bash
qemu-system-x86_64 -M q35 -m 256M -smp 2 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=/tmp/OVMF_VARS.fd \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -cdrom dawn-os.iso -serial stdio
```

---

## Historical Builds Archive

Previous versioned builds are preserved under `builds/<commit-sha>/`.
