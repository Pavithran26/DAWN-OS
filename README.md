# DAWN-OS — QEMU BIOS Boot Validation Evidence

This branch (`qemu-bios`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **legacy BIOS (SeaBIOS)**.

---

## Verification Summary

- **Source Commit:** [`1bff56c`](https://github.com/Pavithran26/DAWN-OS/commit/1bff56c61f214cc6304fa3619df9a91dc4927f38)
- **BIOS Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 80 / 80 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45334064499)
- **Timestamp:** `2026-11-24T05:39:52Z`

---

## Files

- `latest/qemu-bios-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-bios-smoke.log` — Headless boot log for production desktop ISO.
- `latest/bios-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
