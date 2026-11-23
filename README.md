# DAWN-OS — Self-Test Kernel ELF Binaries

This branch (`kernel-test`) hosts self-test 64-bit ELF kernel executables (`dawn-kernel-test.elf`) compiled with `-DDAWN_KERNEL_TEST=1` and comprehensive self-test harnesses.

---

## Specifications

- **Source Commit:** [`b80d048`](https://github.com/Pavithran26/DAWN-OS/commit/b80d048a2e38774b47749f40f61ff8b421fcd0e7)
- **Flags:** `-DDAWN_KERNEL_TEST=1`
- **Tests Embedded:** All 69 hardware and subsystem unit tests
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45131330303)

---

## Files

- `latest/dawn-kernel-test.elf` — Self-test kernel binary.
- `latest/dawn-kernel-test-headers.txt` — Section and program headers.
- `manifest.json` — Metadata.
- `SHA256SUMS` — SHA-256 verification sums.
