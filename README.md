# DAWN-OS — QEMU UEFI Boot Validation Evidence

This branch (`qemu-uefi`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **modern 64-bit UEFI (OVMF)**.

---

## Verification Summary

- **Source Commit:** [`9bf1ca3`](https://github.com/Pavithran26/DAWN-OS/commit/9bf1ca3436aea9ebf7d71b552deb57701a12a86f)
- **UEFI Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 64 / 64 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45121979973)
- **Timestamp:** `2026-11-23T06:08:28Z`

---

## Files

- `latest/qemu-uefi-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-uefi-smoke.log` — Headless boot log for production desktop ISO.
- `latest/uefi-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
