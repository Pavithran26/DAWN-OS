# DAWN-OS — QEMU UEFI Boot Validation Evidence

This branch (`qemu-uefi`) stores serial logs, execution traces, and diagnostic evidence verifying DAWN-OS boots cleanly under **modern 64-bit UEFI (OVMF)**.

---

## Verification Summary

- **Source Commit:** [`7bdbc07`](https://github.com/Pavithran26/DAWN-OS/commit/7bdbc071df879513ffd427876177c69a93b45a2f)
- **UEFI Result:** PASSED (exit 33, 0 FAIL)
- **Tests Passed:** 64 / 64 (0 failures)
- **CI Run:** [Actions Run](https://github.com/Pavithran26/DAWN-OS/actions/runs/45119599891)
- **Timestamp:** `2026-11-23T05:44:36Z`

---

## Files

- `latest/qemu-uefi-serial.log` — Raw COM1 serial console log.
- `latest/uefi-test-summary.txt` — Structured test breakdown.
- `manifest.json` — Complete build metadata.
- `SHA256SUMS` — SHA-256 verification sums.
