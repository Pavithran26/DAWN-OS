# DAWN-OS — QEMU UEFI Boot Validation Evidence

This branch (`qemu-uefi`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **modern 64-bit UEFI (OVMF)**.

---

## Verification Summary

- **Source Commit:** [`9d0a453`](https://github.com/Pavithran26/DAWN-OS/commit/9d0a4532f258a9e2ee907e54931464f0ca4e991c)
- **UEFI Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 83 / 83 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45359490196)
- **Timestamp:** `2026-11-24T08:52:19Z`

---

## Files

- `latest/qemu-uefi-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-uefi-smoke.log` — Headless boot log for production desktop ISO.
- `latest/uefi-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
