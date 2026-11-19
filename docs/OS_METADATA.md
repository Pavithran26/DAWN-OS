# OS Metadata, Developer Identity and Support

## 1. Purpose

`config/os_metadata.json` is the single authoritative source for DAWN-OS identity:
OS version, developer details, website, repository, support links, branding and
copyright. No application should hardcode a website, developer name or version.

The same data powers:

- Settings → About DAWN-OS
- Settings → Developer Credits
- Settings → Help & Support
- Settings → System Information
- Desktop taskbar footer (`DAWN-OS | Developed by Pavithran | pavithran26.net`)
- `uname`/version reporting is being migrated to this source

## 2. Schema (schema_version 1)

Top-level objects: `os`, `developer`, `project`, `support`, `branding`, `copyright`.

| Section | Field | Required | Notes |
|---|---|---|---|
| root | `schema_version` | yes | Must be `1`. Unsupported values are rejected. |
| `os` | `name`, `display_name`, `description`, `architecture` | yes | `architecture` must be `x86_64`. |
| `os` | `version` | yes | Semantic version `MAJOR.MINOR.PATCH[-prerelease]`. |
| `os` | `channel` | yes | Free-form release channel string. |
| `os` | `build_id`, `build_date`, `kernel_version` | no | `null` in JSON; filled by the build generator. |
| `developer` | `name`, `display_name`, `username`, `role`, `website`, `github_profile` | yes | URLs must be `http://` or `https://`. |
| `project` | `name`, `repository`, `issues_url` | yes | URLs validated. |
| `project` | `documentation_url`, `license` | no | May be `null`. |
| `support` | `website`, `bug_report_url`, `feature_request_url` | yes | URLs validated. |
| `support` | `documentation_url`, `community_url`, `email` | no | May be `null`; never invent an email or social account. |
| `branding` | `logo_asset`, `primary_color`, `design_language` | yes | Color `emerald`, design `neo-liquid-glass`; logo is a relative path. |
| `copyright` | `holder`, `notice` | yes | `year` may be `null`; do not invent one. |

Optional fields that are `null` stay null in generated metadata. The runtime view
returns `"Not available"` for presentation instead of substituting a fabricated
value.

## 3. Validation rules

`tools/generate_metadata.py` rejects (non-zero exit, no header written):

- missing or `null` required strings;
- unsupported schema versions;
- non-semantic versions (`0.1`, `v0.1.0`, `0.1.0.4`);
- unsupported architecture identifiers;
- URLs without `http://`/`https://`, with an empty host, whitespace or control characters;
- invalid branding (`primary_color`, `design_language`, absolute/logo paths containing `..`);
- implausible `copyright.year` values.

Metadata is data, never executable code. There is no JSON parser in the kernel:
validation happens at build time, and `kernel/os_metadata.c` re-validates the
generated constants at runtime (`schema_version`, semver, architecture, URLs,
branding) before exposing them.

`dawn_url_is_valid()` and `dawn_url_copy()` are used before any link is opened.
`dawn_escape_text()` escapes HTML/control-significant characters before rendering
metadata text.

## 4. Build metadata generation

```sh
python3 tools/generate_metadata.py \
    --input config/os_metadata.json \
    --output include/dawn_metadata_generated.h
```

Make target: `make metadata`. The header is also regenerated automatically when
`config/os_metadata.json` or the generator script changes.

- **Build ID**: `git rev-parse HEAD`.
- **Build date**: `SOURCE_DATE_EPOCH` when set, otherwise the Git commit date
  (reproducible). Format `YYYY-MM-DD`.
- **Kernel version**: `git describe --tags --always --dirty`, prefixed `dawn-`.
- **Release version**: from `os.version` in the JSON, never hardcoded in the UI.

Current release value is `1.0.0`, matching the existing `uname` and shell version
strings. `channel` remains `development` because no release channel is configured
in the repository. The repository uses The Unlicense (`LICENSE`); the `license` field
is set to `"The Unlicense"`. `copyright.year` remains `null` deliberately.

## 5. Updating identity

1. Edit `config/os_metadata.json` only.
2. Run `make metadata` (or let the build regenerate the header).
3. Update `docs/OS_METADATA.md` if schema or field meaning changed.
4. Do not edit `include/dawn_metadata_generated.h` by hand; it is generated.
5. Do not add developer details or URLs directly to application source files.

Website/support links: change `developer.website`, `project.repository`,
`project.issues_url`, and the `support` URLs together so About, Credits, Help and
the footer stay consistent.

## 6. Runtime API

`include/os_metadata.h`:

- `dawn_metadata_initialize()` / `dawn_metadata_is_valid()`
- `dawn_metadata_get()` — read-only `struct dawn_metadata_view`
- `dawn_metadata_or_unavailable()` — safe optional-field display
- `dawn_url_is_valid()`, `dawn_url_copy()` — link validation
- `dawn_escape_text()` — safe rendering

`include/system_info.h`:

- `dawn_system_info_refresh()` — CPU model (CPUID 0x80000002–4), physical/free
  memory, LAPIC ticks/Hz and uptime, firmware boot mode (detected via Limine EFI
  system table request: "UEFI (Limine)" vs "BIOS (Limine)"), storage inventory
  and filesystem summary from real kernel APIs.
- `dawn_decimal_string()`, `dawn_human_bytes()` — formatting helpers.

Values that cannot be detected are reported as `Not available`. Nothing is fabricated.

## 7. Desktop and Settings integration

Settings tab order: Display, Input, Audio, Network, Time, Appearance, About,
Credits, Help, Sys Info. The compact 28 px tab stride keeps all ten tabs inside
the 348 px visible client area. Window client area clicks are routed via
`win->on_click` to `settings_window_handle_click()`, making tabs, buttons, and
links responsive. Link rectangles are recorded during render and hit-tested on
click; only validated URLs become links.

Help & Support is organized into four navigable categories:
1. Getting Started (Desktop, Start Menu, Terminal, Files, Settings, website, repo)
2. Installation & BIOS/UEFI Troubleshooting (Limine dual boot, storage controllers, black screen / APIC freeze remediation, rescue shell)
3. Subsystems & Applications Help (File Explorer, Terminal Ring 3 execution, Task Manager, Storage Manager, audio chime; unimplemented items marked "Coming Soon")
4. FAQ & Known Issues (Independent custom OS architecture confirmation, VFS filesystems, known limitations marked "Coming Soon", GitHub issue tracker links)

The application launcher and desktop icons provide direct entry points:
- Desktop icons: "This PC" (opens System Info), "Files", "Terminal", "Task Mgr", "Settings", "About OS", and "Help".
- Start Menu items: Terminal, File Explorer, Task Manager, Settings, Help & Support, About DAWN-OS, and Shut Down.
- Taskbar footer: underlines developer domain `pavithran26.net` and routes clicks through `desktop_open_url()`.

**Production loop integration:**
The production kernel path in `kernel/main.c` executes an active event loop:
1. Calibrates and starts the LAPIC periodic timer unconditionally (`apic_local_timer_calibrate` and `apic_local_timer_start_calibrated_periodic`).
2. Polls hardware input devices via `input_poll_hardware()`.
3. Processes all queued events through `desktop_handle_input_event()` with press-edge detection (`mevt->left_button && !prev_left_button`).
4. Composites the desktop and window tree to the hardware framebuffer using `desktop_render(fb)`.
5. Advances kernel threads and userspace tasks via `scheduler_run_until_idle()`.
6. Decrements notification TTL (`ticks_remaining`) on each render tick, clearing expired toasts.
7. Routes Start Menu "Shut Down" directly to `power_shutdown()`.
8. Enforces window single-instance focus (`wm_find_window_by_title`) and cleans up window lifecycle references via `win->on_close`.

**Known limitation:** this build has no system web browser yet. A click on a web link validates the URL and displays a notification with the target address rather than claiming a page was opened. Wiring an embedded web browser remains future work.

## 8. Source-level validation (not executed in this phase)

- `os_metadata_self_test()` — semver, URL validation, optional fields, escaping,
  URL copy and footer construction.
- `system_info_self_test()` — snapshot refresh and formatting helpers.
- `settings_self_test()` — new tabs, link generation and help navigation.

Tests are registered in `kernel/main.c` and compiled into the test target; they
are intentionally not run during implementation phases.
