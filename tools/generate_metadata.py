#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Validate config/os_metadata.json and generate the read-only runtime header.

This script is the ONLY bridge between the authoritative JSON configuration and
C source. It never invents values: a missing required field, a malformed URL,
an unsupported schema version, or a bad semantic version is a hard error.

Dynamic build fields (build_id, build_date, kernel_version) are filled from the
build environment / Git. They are intentionally null in the JSON file.

Usage:
    python3 tools/generate_metadata.py \
        --input config/os_metadata.json \
        --output include/dawn_metadata_generated.h
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional
from urllib.parse import urlparse

SUPPORTED_SCHEMA_VERSIONS = (1,)
SUPPORTED_ARCHITECTURES = ("x86_64",)
SUPPORTED_COLORS = ("emerald",)
SUPPORTED_DESIGN_LANGUAGES = ("neo-liquid-glass",)
SEMVER_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$")


class MetadataError(Exception):
    """Raised for any invalid metadata. Never silently repaired."""


def _require(mapping: Dict[str, Any], key: str, path: str) -> Any:
    if key not in mapping:
        raise MetadataError(f"missing required field: {path}.{key}")
    value = mapping[key]
    if value is None:
        raise MetadataError(f"required field is null: {path}.{key}")
    return value


def _nonempty_string(value: Any, path: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise MetadataError(f"expected non-empty string at {path}")
    return value


def _optional_string(value: Any, path: str) -> Optional[str]:
    if value is None:
        return None
    return _nonempty_string(value, path)


def _validate_url(value: Any, path: str) -> str:
    text = _nonempty_string(value, path)
    parsed = urlparse(text)
    if parsed.scheme not in ("http", "https"):
        raise MetadataError(f"{path} must use http:// or https://")
    if not parsed.netloc or any(ch.isspace() for ch in text):
        raise MetadataError(f"{path} is not a valid URL: {text}")
    return text


def _validate_optional_url(value: Any, path: str) -> Optional[str]:
    if value is None:
        return None
    return _validate_url(value, path)


def _validate_semver(value: Any, path: str) -> str:
    text = _nonempty_string(value, path)
    if not SEMVER_RE.match(text):
        raise MetadataError(f"{path} is not a semantic version: {text}")
    return text


def _validate_branding(value: Any, path: str) -> Dict[str, Any]:
    if not isinstance(value, dict):
        raise MetadataError(f"{path} must be an object")
    logo = _nonempty_string(_require(value, "logo_asset", path), f"{path}.logo_asset")
    if logo.startswith("/") or ".." in Path(logo).parts:
        raise MetadataError(f"{path}.logo_asset must be a relative path inside the repository")
    color = _nonempty_string(_require(value, "primary_color", path), f"{path}.primary_color")
    if color not in SUPPORTED_COLORS:
        raise MetadataError(f"{path}.primary_color unsupported: {color}")
    design = _nonempty_string(_require(value, "design_language", path), f"{path}.design_language")
    if design not in SUPPORTED_DESIGN_LANGUAGES:
        raise MetadataError(f"{path}.design_language unsupported: {design}")
    return {"logo_asset": logo, "primary_color": color, "design_language": design}


def validate(data: Any) -> Dict[str, Any]:
    """Validate the full document. Return a normalized view or raise MetadataError."""
    if not isinstance(data, dict):
        raise MetadataError("root must be a JSON object")

    schema = _require(data, "schema_version", "root")
    if not isinstance(schema, int) or isinstance(schema, bool) or schema not in SUPPORTED_SCHEMA_VERSIONS:
        raise MetadataError(f"unsupported schema_version: {schema!r}")

    sections = ("os", "developer", "project", "support", "branding", "copyright")
    for name in sections:
        section = data.get(name)
        if not isinstance(section, dict):
            raise MetadataError(f"missing or invalid section: {name}")

    os_section = data["os"]
    developer = data["developer"]
    project = data["project"]
    support = data["support"]
    copyright = data["copyright"]

    os_name = _nonempty_string(_require(os_section, "name", "os"), "os.name")
    display_name = _nonempty_string(_require(os_section, "display_name", "os"), "os.display_name")
    description = _nonempty_string(_require(os_section, "description", "os"), "os.description")
    arch = _nonempty_string(_require(os_section, "architecture", "os"), "os.architecture")
    if arch not in SUPPORTED_ARCHITECTURES:
        raise MetadataError(f"unsupported os.architecture: {arch}")
    version = _validate_semver(_require(os_section, "version", "os"), "os.version")
    channel = _nonempty_string(_require(os_section, "channel", "os"), "os.channel")

    dev_name = _nonempty_string(_require(developer, "name", "developer"), "developer.name")
    dev_display = _nonempty_string(_require(developer, "display_name", "developer"), "developer.display_name")
    dev_username = _nonempty_string(_require(developer, "username", "developer"), "developer.username")
    dev_role = _nonempty_string(_require(developer, "role", "developer"), "developer.role")
    dev_website = _validate_url(_require(developer, "website", "developer"), "developer.website")
    dev_github = _validate_url(_require(developer, "github_profile", "developer"), "developer.github_profile")

    project_name = _nonempty_string(_require(project, "name", "project"), "project.name")
    repository = _validate_url(_require(project, "repository", "project"), "project.repository")
    doc_url = _validate_optional_url(project.get("documentation_url"), "project.documentation_url")
    issues_url = _validate_url(_require(project, "issues_url", "project"), "project.issues_url")
    license_name = _optional_string(project.get("license"), "project.license")

    support_website = _validate_url(_require(support, "website", "support"), "support.website")
    support_doc = _validate_optional_url(support.get("documentation_url"), "support.documentation_url")
    community = _validate_optional_url(support.get("community_url"), "support.community_url")
    email = support.get("email")
    if email is not None and (not isinstance(email, str) or not email or "\n" in email):
        raise MetadataError("support.email must be a single-line string or null")
    bug_url = _validate_url(_require(support, "bug_report_url", "support"), "support.bug_report_url")
    feature_url = _validate_url(_require(support, "feature_request_url", "support"), "support.feature_request_url")

    branding = _validate_branding(data["branding"], "branding")

    holder = _nonempty_string(_require(copyright, "holder", "copyright"), "copyright.holder")
    notice = _nonempty_string(_require(copyright, "notice", "copyright"), "copyright.notice")
    year = copyright.get("year")
    if year is not None and (not isinstance(year, int) or isinstance(year, bool) or not (1970 <= year <= 9999)):
        raise MetadataError(f"copyright.year must be a plausible year or null: {year!r}")

    return {
        "schema_version": schema,
        "os": {
            "name": os_name, "display_name": display_name, "description": description,
            "architecture": arch, "version": version, "channel": channel,
            "build_id": _optional_string(os_section.get("build_id"), "os.build_id"),
            "build_date": _optional_string(os_section.get("build_date"), "os.build_date"),
            "kernel_version": _optional_string(os_section.get("kernel_version"), "os.kernel_version"),
        },
        "developer": {
            "name": dev_name, "display_name": dev_display, "username": dev_username,
            "role": dev_role, "website": dev_website, "github_profile": dev_github,
        },
        "project": {
            "name": project_name, "repository": repository, "documentation_url": doc_url,
            "issues_url": issues_url, "license": license_name,
        },
        "support": {
            "website": support_website, "documentation_url": support_doc,
            "community_url": community, "email": email,
            "bug_report_url": bug_url, "feature_request_url": feature_url,
        },
        "branding": branding,
        "copyright": {"holder": holder, "year": year, "notice": notice},
    }


def _git(args: List[str], repo_root: Path) -> Optional[str]:
    try:
        out = subprocess.run(
            ["git", *args], cwd=str(repo_root), check=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
        return out.stdout.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def build_values(meta: Dict[str, Any], repo_root: Path) -> Dict[str, Any]:
    """Fill only the three dynamic fields; never touch identity fields."""
    build_id = meta["os"]["build_id"]
    if build_id is None:
        build_id = _git(["rev-parse", "HEAD"], repo_root) or "unknown"

    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is not None:
        try:
            stamp = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        except ValueError as exc:
            raise MetadataError(f"invalid SOURCE_DATE_EPOCH: {exc}") from exc
    else:
        commit_date = _git(["show", "-s", "--format=%cI", "HEAD"], repo_root)
        if commit_date:
            try:
                stamp = datetime.datetime.fromisoformat(commit_date)
            except ValueError:
                stamp = datetime.datetime.now(datetime.timezone.utc)
        else:
            stamp = datetime.datetime.now(datetime.timezone.utc)
    build_date = meta["os"]["build_date"] or stamp.strftime("%Y-%m-%d")

    kernel_version = meta["os"]["kernel_version"]
    if kernel_version is None:
        describe = _git(["describe", "--tags", "--always", "--dirty"], repo_root)
        kernel_version = f"dawn-{describe}" if describe else "dawn-unknown"

    result = dict(meta)
    result["os"] = dict(meta["os"])
    result["os"]["build_id"] = build_id
    result["os"]["build_date"] = build_date
    result["os"]["kernel_version"] = kernel_version
    return result


def _c_string(value: str) -> str:
    out = ['"']
    for b in value.encode("utf-8"):
        if b == ord("\\"):
            out.append("\\\\")
        elif b == ord('"'):
            out.append('\\"')
        elif b == ord("\n"):
            out.append("\\n")
        elif b == ord("\r"):
            out.append("\\r")
        elif b == ord("\t"):
            out.append("\\t")
        elif 32 <= b <= 126:
            out.append(chr(b))
        else:
            out.append(f"\\{b:03o}")
    out.append('"')
    return "".join(out)


def render(meta: Dict[str, Any]) -> str:
    osd = meta["os"]
    dev = meta["developer"]
    project = meta["project"]
    support = meta["support"]
    branding = meta["branding"]
    copyright = meta["copyright"]

    def define(name: str, value: Optional[str]) -> str:
        if value is None:
            return ""
        return f"#define {name} {_c_string(value)}\n"

    def flag(name: str, value: Any) -> str:
        return f"#define {name} {1 if value is not None else 0}\n"

    lines: List[str] = []
    lines.append("/* Generated by tools/generate_metadata.py - DO NOT EDIT. */\n")
    lines.append("#ifndef DAWN_METADATA_GENERATED_H\n#define DAWN_METADATA_GENERATED_H\n\n")
    lines.append(f"#define DAWN_META_SCHEMA_VERSION {meta['schema_version']}\n\n")

    for name, key in (
        ("DAWN_META_OS_NAME", "name"),
        ("DAWN_META_OS_DISPLAY_NAME", "display_name"),
        ("DAWN_META_OS_DESCRIPTION", "description"),
        ("DAWN_META_OS_ARCHITECTURE", "architecture"),
        ("DAWN_META_OS_VERSION", "version"),
        ("DAWN_META_OS_CHANNEL", "channel"),
        ("DAWN_META_BUILD_ID", "build_id"),
        ("DAWN_META_BUILD_DATE", "build_date"),
        ("DAWN_META_KERNEL_VERSION", "kernel_version"),
    ):
        lines.append(define(name, osd[key]))
    lines.append("\n")

    for name, key in (
        ("DAWN_META_DEVELOPER_NAME", "name"),
        ("DAWN_META_DEVELOPER_DISPLAY_NAME", "display_name"),
        ("DAWN_META_DEVELOPER_USERNAME", "username"),
        ("DAWN_META_DEVELOPER_ROLE", "role"),
        ("DAWN_META_DEVELOPER_WEBSITE", "website"),
        ("DAWN_META_DEVELOPER_GITHUB", "github_profile"),
    ):
        lines.append(define(name, dev[key]))
    lines.append("\n")

    for name, key in (
        ("DAWN_META_PROJECT_NAME", "name"),
        ("DAWN_META_REPOSITORY_URL", "repository"),
        ("DAWN_META_ISSUES_URL", "issues_url"),
    ):
        lines.append(define(name, project[key]))
    lines.append(define("DAWN_META_PROJECT_DOC_URL", project["documentation_url"]))
    lines.append(define("DAWN_META_LICENSE", project["license"]))
    lines.append("\n")

    for name, key in (
        ("DAWN_META_SUPPORT_WEBSITE", "website"),
        ("DAWN_META_BUG_REPORT_URL", "bug_report_url"),
        ("DAWN_META_FEATURE_REQUEST_URL", "feature_request_url"),
    ):
        lines.append(define(name, support[key]))
    lines.append(define("DAWN_META_SUPPORT_DOC_URL", support["documentation_url"]))
    lines.append(define("DAWN_META_COMMUNITY_URL", support["community_url"]))
    lines.append(define("DAWN_META_SUPPORT_EMAIL", support["email"]))
    lines.append("\n")

    lines.append(define("DAWN_META_LOGO_ASSET", branding["logo_asset"]))
    lines.append(define("DAWN_META_PRIMARY_COLOR", branding["primary_color"]))
    lines.append(define("DAWN_META_DESIGN_LANGUAGE", branding["design_language"]))
    lines.append("\n")
    lines.append(define("DAWN_META_COPYRIGHT_HOLDER", copyright["holder"]))
    lines.append(define("DAWN_META_COPYRIGHT_NOTICE", copyright["notice"]))
    lines.append(define("DAWN_META_COPYRIGHT_YEAR", str(copyright["year"]) if copyright["year"] is not None else None))
    lines.append("\n")

    for name, value in (
        ("DAWN_META_HAS_PROJECT_DOC_URL", project["documentation_url"]),
        ("DAWN_META_HAS_LICENSE", project["license"]),
        ("DAWN_META_HAS_SUPPORT_DOC_URL", support["documentation_url"]),
        ("DAWN_META_HAS_COMMUNITY_URL", support["community_url"]),
        ("DAWN_META_HAS_SUPPORT_EMAIL", support["email"]),
        ("DAWN_META_HAS_COPYRIGHT_YEAR", copyright["year"]),
    ):
        lines.append(flag(name, value))

    lines.append("\n#endif /* DAWN_METADATA_GENERATED_H */\n")
    return "".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="config/os_metadata.json")
    parser.add_argument("--output", default="include/dawn_metadata_generated.h")
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    input_path = Path(args.input)
    if not input_path.is_absolute():
        input_path = root / input_path
    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = root / output_path

    try:
        with input_path.open("r", encoding="utf-8") as handle:
            raw = json.load(handle)
        meta = validate(raw)
        meta = build_values(meta, root)
        rendered = render(meta)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8")
    except (OSError, json.JSONDecodeError, MetadataError) as exc:
        print(f"metadata generation failed: {exc}", file=sys.stderr)
        return 1

    print(f"generated {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
