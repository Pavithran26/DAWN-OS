# DAWN-OS — QEMU UEFI Boot Validation Evidence

This branch (`qemu-uefi`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **modern 64-bit UEFI (OVMF)**.

---

## Verification Summary

- **Source Commit:** [`b80d048`](https://github.com/Pavithran26/DAWN-OS/commit/b80d048a2e38774b47749f40f61ff8b421fcd0e7)
- **UEFI Self-Test Result:** PASSED (exit 33, 0 FAIL)
- **Production ISO Smoke Boot:** PASSED (desktop loop reached)
- **Tests Passed:** 69 / 69 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45131330303)
- **Timestamp:** `2026-11-23T07:29:51Z`

---

## Files

- `latest/qemu-uefi-serial.log` — Full COM1 serial console log for self-test suite.
- `latest/qemu-uefi-smoke.log` — Headless boot log for production desktop ISO.
- `latest/uefi-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
