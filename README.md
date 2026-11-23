# DAWN-OS — Self-Test Kernel ELF Binaries

This branch (`kernel-test`) hosts self-test 64-bit ELF kernel executables (`dawn-kernel-test.elf`) compiled with `-DDAWN_KERNEL_TEST=1` and comprehensive self-test harnesses.

---

## Specifications

- **Source Commit:** [`9bf1ca3`](https://github.com/Pavithran26/DAWN-OS/commit/9bf1ca3436aea9ebf7d71b552deb57701a12a86f)
- **Flags:** `-DDAWN_KERNEL_TEST=1`
- **Tests Embedded:** All 64 hardware and subsystem unit tests
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45121979973)

---

## Files

- `latest/dawn-kernel-test.elf` — Self-test kernel binary.
- `latest/dawn-kernel-test-headers.txt` — Section and program headers.
- `manifest.json` — Metadata.
- `SHA256SUMS` — SHA-256 verification sums.
