# DAWN-OS — Self-Test Kernel ELF Binaries

This branch (`kernel-test`) hosts self-test 64-bit ELF kernel executables (`dawn-kernel-test.elf`) compiled with `-DDAWN_KERNEL_TEST=1` and comprehensive self-test harnesses.

---

## Specifications

- **Source Commit:** [`ac3db5c`](https://github.com/Pavithran26/DAWN-OS/commit/ac3db5c6c62c8793c1eac50d2f0aa9bf3e187949)
- **Flags:** `-DDAWN_KERNEL_TEST=1`
- **Tests Embedded:** All 77 hardware and subsystem unit tests
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45169544071)

---

## Files

- `latest/dawn-kernel-test.elf` — Self-test kernel binary.
- `latest/dawn-kernel-test-headers.txt` — Section and program headers.
- `manifest.json` — Metadata.
- `SHA256SUMS` — SHA-256 verification sums.
