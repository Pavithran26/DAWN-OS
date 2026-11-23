# DAWN-OS — Self-Test Kernel ELF Binaries

This branch (`kernel-test`) hosts self-test 64-bit ELF kernel executables (`dawn-kernel-test.elf`) compiled with `-DDAWN_KERNEL_TEST=1` and comprehensive self-test harnesses.

---

## Specifications

- **Source Commit:** [`ae51f7f`](https://github.com/Pavithran26/DAWN-OS/commit/ae51f7fc2569f4c2c4d54a02c4b854452165fa79)
- **Flags:** `-DDAWN_KERNEL_TEST=1`
- **Tests Embedded:** All 69 hardware and subsystem unit tests
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45141439390)

---

## Files

- `latest/dawn-kernel-test.elf` — Self-test kernel binary.
- `latest/dawn-kernel-test-headers.txt` — Section and program headers.
- `manifest.json` — Metadata.
- `SHA256SUMS` — SHA-256 verification sums.
