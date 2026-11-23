# DAWN-OS — QEMU UEFI Boot Validation Evidence

This branch (`qemu-uefi`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **modern 64-bit UEFI (OVMF)**.

---

## Verification Summary

- **Source Commit:** [`cd95efd`](https://github.com/Pavithran26/DAWN-OS/commit/cd95efd2afd3ea0eb4b793814fcf64435ea33f7f)
- **UEFI Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 73 / 73 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45153537649)
- **Timestamp:** `2026-11-23T10:14:26Z`

---

## Files

- `latest/qemu-uefi-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-uefi-smoke.log` — Headless boot log for production desktop ISO.
- `latest/uefi-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
