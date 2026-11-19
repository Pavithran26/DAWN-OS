# Phase L — System Settings & Configuration Status

## 1. Phase Summary

Phase L implements the central System Settings framework, hardware profile configuration, user preferences, persistent file storage engine (`/boot/system.cfg`), and the graphical Settings application for DAWN-OS. DAWN-OS is an independent, custom bare-metal x86_64 operating system built completely from scratch without Linux or Windows NT kernel code or dependencies.

## 2. Completed Features

### Settings Engine & Configuration Framework (`include/settings.h`, `kernel/settings.c`)
- Comprehensive Settings Model (`struct system_settings`):
  - **Display**: Resolution (`1024x768`, `1280x720`, `1920x1080`), color depth (32-bit ARGB8888), refresh rate (60Hz).
  - **Input**: Keyboard repeat delay (250 ms), repeat rate (30 cps), mouse sensitivity multiplier (1..10), acceleration toggle.
  - **Audio**: Master volume (0..100%), mute status, tone generation hooks.
  - **Network**: Static/DHCP configuration, IP address, subnet mask, default gateway, DNS server.
  - **Date & Time**: Hardware RTC synchronization, timezone offset in minutes, 24-hour vs 12-hour format.
  - **Appearance**: Emerald Green accent color (`#00C853`), frosted glass opacity (0..255), desktop wallpaper selection.
  - **Power**: Power profiles (`POWER_PROFILE_BALANCED`, `POWER_PROFILE_PERFORMANCE`, `POWER_PROFILE_POWER_SAVER`), screen blanking timeout.
- Subsystem Application:
  - Dynamically updates active hardware subsystems upon setting change (adjusts audio master volume/mute in `audio.c`, configures network interfaces in `net.c`).
- Persistent Configuration Storage:
  - Default path: `/boot/system.cfg`.
  - Human-readable key-value serializer: formats settings into structured text.
  - Fault-tolerant parser: deserializes configuration, handles unknown or missing keys gracefully, and restores default values.
  - Range clamping: ensures volume is bounded between 0..100, sensitivity between 1..10, and validates network IP formats.
- Graphical Settings Application:
  - Dedicated Window Manager window: `"Settings"` (560x380 pixels).
  - Left navigation sidebar (140px width) rendered with dark glass style (`GFX_COLOR_PANEL_DARK`), housing 10 navigation tabs at a compact 28px stride:
    1. **Display**: Resolution, color depth, driver info, and resolution preset buttons.
    2. **Input**: Mouse sensitivity gauge, acceleration status, keyboard repeat parameters.
    3. **Audio**: Master volume gauge, mute indicator, and interactive "Play Chime" audio test button.
    4. **Network**: Interface list (`eth0`), current IP, subnet mask, gateway, DNS, and manual/static mode.
    5. **Time**: Hardware RTC status, UTC offset, and time format.
    6. **Appearance**: Neo-Liquid Glass theme colors, opacity percentage, and visual styling.
    7. **About**: Real OS version, channel, developer, website/GitHub/repository links, build and kernel metadata from the central metadata API.
    8. **Credits**: Developer Credits with Pavithran, project owner, role, clickable website/GitHub and the footer string.
    9. **Help**: Three-page Help & Support Center (Getting Started, Install & Boot, Reference) with Prev/Next navigation, issue-tracker links and "Coming Soon" labels for unfinished features.
    10. **Sys Info**: System Information from real kernel APIs (CPUID brand string, memory, LAPIC uptime, boot mode, storage and filesystem summary) with "Not available" for undetectable values.
  - Interactive mouse event routing: clicking sidebar tabs switches views; clicking hyperlinks validates the URL then delegates to the desktop link handler; clicking buttons triggers actions (e.g. sound chime, help page navigation).
- Self-test (`settings_self_test`):
  - Validates default parameter assignment.
  - Validates key=value text serialization and round-trip deserialization.
  - Validates boundary clamping on corrupted or out-of-range inputs.
  - Validates Window Manager integration, tab switching, and event handling.

## 3. Kernel Integration
- Integrated `settings_initialize()` into kernel bootstrap in `kernel/main.c`.
- Added `settings_self_test()` to the kernel self-test harness in `kernel/main.c` under `DAWN_KERNEL_TEST`.
- Updated `Makefile` with `settings.o` and `settings-test.o` rules.
- Linked `settings.o` into `build/dawn-kernel.elf` and `settings-test.o` into `build/dawn-kernel-test.elf`.

## 4. Developer Identity and OS Metadata

- Added `config/os_metadata.json` as the single source of OS/developer/project/support identity.
- Added `tools/generate_metadata.py` (schema/URL/semver validation and dynamic build metadata) and generated `include/dawn_metadata_generated.h`.
- Added `include/os_metadata.h` / `kernel/os_metadata.c`: read-only runtime view, URL validation, optional-field handling and safe text escaping.
- Added `include/system_info.h` / `kernel/system_info.c`: CPUID model, memory, LAPIC uptime, storage and filesystem data from real kernel APIs; undetectable values show "Not available".
- Added a clickable developer footer to the desktop and a validated `desktop_open_url()` link handler.
- Registered `os_metadata_self_test()` and `system_info_self_test()` in the kernel test harness (source-level only; not executed during implementation).
- See `docs/OS_METADATA.md` for schema, update instructions and known limitations.
