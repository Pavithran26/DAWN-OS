# DAWN-OS — Self-Test Kernel ELF Binaries

This branch (`kernel-test`) hosts self-test 64-bit ELF kernel executables (`dawn-kernel-test.elf`) compiled with `-DDAWN_KERNEL_TEST=1` and comprehensive self-test harnesses.

---

## Specifications

- **Source Commit:** [`94d4b3e`](https://github.com/Pavithran26/DAWN-OS/commit/94d4b3e03341203f8da68400017af5e8903baafd)
- **Flags:** `-DDAWN_KERNEL_TEST=1`
- **Tests Embedded:** All 79 hardware and subsystem unit tests
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45176616827)

---

## Files

- `latest/dawn-kernel-test.elf` — Self-test kernel binary.
- `latest/dawn-kernel-test-headers.txt` — Section and program headers.
- `manifest.json` — Metadata.
- `SHA256SUMS` — SHA-256 verification sums.
