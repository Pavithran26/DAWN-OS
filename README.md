# DAWN-OS — QEMU BIOS Boot Validation Evidence

This branch (`qemu-bios`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **legacy BIOS (SeaBIOS)**.

---

## Verification Summary

- **Source Commit:** [`ae51f7f`](https://github.com/Pavithran26/DAWN-OS/commit/ae51f7fc2569f4c2c4d54a02c4b854452165fa79)
- **BIOS Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 69 / 69 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45141439390)
- **Timestamp:** `2026-11-23T08:48:36Z`

---

## Files

- `latest/qemu-bios-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-bios-smoke.log` — Headless boot log for production desktop ISO.
- `latest/bios-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
