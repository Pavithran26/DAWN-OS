# DAWN-OS — QEMU BIOS Boot Validation Evidence

This branch (`qemu-bios`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **legacy BIOS (SeaBIOS)**.

---

## Verification Summary

- **Source Commit:** [`b331bef`](https://github.com/Pavithran26/DAWN-OS/commit/b331bef8d30093f529be4312aa1f4b04a1b07e87)
- **BIOS Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 67 / 67 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45125180689)
- **Timestamp:** `2026-11-23T06:37:26Z`

---

## Files

- `latest/qemu-bios-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-bios-smoke.log` — Headless boot log for production desktop ISO.
- `latest/bios-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
