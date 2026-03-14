#!/usr/bin/env python3
"""
validate_submission.py — ESP32-S3 NUT Node UPS Compatibility Validator

Pulls a GitHub issue by number, extracts the serial log and submission fields,
sends them to Claude for analysis, and returns a validation verdict.

Usage:
    python validate_submission.py <issue_number>
    python validate_submission.py 42

Requirements (auto-installed if missing):
    anthropic
    requests

Environment variables required:
    GITHUB_TOKEN    — GitHub personal access token (repo:read scope)
    ANTHROPIC_API_KEY — Anthropic API key

Setup:
    export GITHUB_TOKEN=ghp_xxxxxxxxxxxx
    export ANTHROPIC_API_KEY=sk-ant-xxxxxxxxxxxx
    python validate_submission.py 42
"""

import sys
import os
import subprocess
import json
import re

GITHUB_REPO   = "Driftah9/esp32-s3-nut-node"
CLAUDE_MODEL  = "claude-sonnet-4-20250514"
MAX_LOG_LINES = 200

# ── Dependency bootstrap ───────────────────────────────────────────────────────

def ensure_dependencies():
    """Install missing packages automatically."""
    missing = []
    for pkg in ["anthropic", "requests"]:
        try:
            __import__(pkg)
        except ImportError:
            missing.append(pkg)

    if missing:
        print(f"[setup] Installing missing packages: {', '.join(missing)}")
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "--quiet"] + missing
        )
        print("[setup] Done.\n")

ensure_dependencies()

import anthropic  # noqa: E402 (imported after install)
import requests   # noqa: E402


# ── Environment checks ─────────────────────────────────────────────────────────

def check_env():
    errors = []
    if not os.environ.get("GITHUB_TOKEN"):
        errors.append(
            "GITHUB_TOKEN not set.\n"
            "  Get one at: https://github.com/settings/tokens\n"
            "  Needs: repo (read) scope\n"
            "  Then run: export GITHUB_TOKEN=ghp_xxxx"
        )
    if not os.environ.get("ANTHROPIC_API_KEY"):
        errors.append(
            "ANTHROPIC_API_KEY not set.\n"
            "  Get one at: https://console.anthropic.com/\n"
            "  Then run: export ANTHROPIC_API_KEY=sk-ant-xxxx"
        )
    if errors:
        print("── Missing Environment Variables ─────────────────────────────")
        for e in errors:
            print(f"\n❌  {e}")
        print("\n──────────────────────────────────────────────────────────────")
        sys.exit(1)


# ── GitHub issue fetch ─────────────────────────────────────────────────────────

def fetch_issue(issue_number: int) -> dict:
    """Fetch issue body and metadata from GitHub API."""
    token = os.environ["GITHUB_TOKEN"]
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    url = f"https://api.github.com/repos/{GITHUB_REPO}/issues/{issue_number}"
    resp = requests.get(url, headers=headers, timeout=15)
    if resp.status_code == 404:
        print(f"❌  Issue #{issue_number} not found in {GITHUB_REPO}")
        sys.exit(1)
    resp.raise_for_status()
    return resp.json()


def parse_issue_fields(body: str) -> dict:
    """Extract structured fields from a GitHub issue form body."""
    fields = {}

    patterns = {
        "manufacturer":   r"### UPS Manufacturer\s*\n+(.+?)(?=\n###|\Z)",
        "model":          r"### UPS Model\s*\n+(.+?)(?=\n###|\Z)",
        "vid_pid":        r"### USB VID:PID\s*\n+(.+?)(?=\n###|\Z)",
        "firmware":       r"### Firmware Version\s*\n+(.+?)(?=\n###|\Z)",
        "powers_device":  r"### Does the UPS power the ESP32[^\n]*\n+(.+?)(?=\n###|\Z)",
        "status_reads":   r"### Does UPS status data[^\n]*\n+(.+?)(?=\n###|\Z)",
        "serial_log":     r"### Serial Log[^\n]*\n+```[^\n]*\n([\s\S]+?)```",
        "status_json":    r"### Status JSON[^\n]*\n+```[^\n]*\n([\s\S]+?)```",
        "notes":          r"### Additional Notes\s*\n+([\s\S]+?)(?=\n###|\Z)",
    }

    for key, pattern in patterns.items():
        m = re.search(pattern, body, re.IGNORECASE)
        fields[key] = m.group(1).strip() if m else ""

    # Truncate serial log to MAX_LOG_LINES
    if fields.get("serial_log"):
        lines = fields["serial_log"].splitlines()
        if len(lines) > MAX_LOG_LINES:
            fields["serial_log"] = "\n".join(lines[:MAX_LOG_LINES])
            fields["serial_log"] += f"\n... [truncated at {MAX_LOG_LINES} lines]"

    return fields


# ── Claude validation ──────────────────────────────────────────────────────────

VALIDATION_PROMPT = """You are validating a UPS compatibility report for the ESP32-S3 NUT Node firmware.
This is an open-source project that uses an ESP32-S3 as a USB Host UPS monitor and NUT server.

Your job is to analyze the serial log and submission details to determine whether this UPS
genuinely works with the firmware.

## Submission Details

**Manufacturer:** {manufacturer}
**Model:** {model}
**VID:PID:** {vid_pid}
**Firmware Version:** {firmware}
**UPS powers ESP32:** {powers_device}
**Status data visible in portal:** {status_reads}
**Additional notes:** {notes}

## Serial Log (first {max_lines} lines)

```
{serial_log}
```

{status_json_section}

## Validation Criteria

Check the serial log for evidence of:

1. **USB enumeration success** — look for lines containing VID:PID, "USB device connected",
   "hid_host", or similar. The VID:PID in the log should match the submitted VID:PID.

2. **HID descriptor parsed** — look for "HID descriptor", "report descriptor", "usage page",
   or "Power Device" entries. Fatal parse errors are a red flag.

3. **UPS data being read** — look for battery charge values, status strings (OL/OB/CHRG),
   voltage readings, or NUT variable assignments like "battery.charge=".

4. **NUT server ready** — look for "NUT server", "tcp/3493", "ups online", or similar.

5. **No critical errors** — check for crash/reboot loops, USB host errors, or HID failures
   that would prevent normal operation.

## Response Format

Respond with a structured verdict in exactly this format:

VERDICT: [CONFIRMED / PARTIAL / NEEDS-MORE-INFO / NOT-SUPPORTED]

CONFIDENCE: [HIGH / MEDIUM / LOW]

SUMMARY:
<2-3 sentences summarizing what the log shows and why you reached this verdict>

EVIDENCE:
- <specific log line or evidence supporting the verdict>
- <another piece of evidence>
- <etc>

CONCERNS:
- <any issues, missing data, or red flags — or "None" if clean>

RECOMMENDATION:
<What action the maintainer should take — apply 'confirmed' label, ask for more info, etc.>
"""


def validate_with_claude(fields: dict, issue_number: int, issue_title: str) -> str:
    """Send parsed issue fields to Claude and return the validation verdict."""
    client = anthropic.Anthropic(api_key=os.environ["ANTHROPIC_API_KEY"])

    status_json_section = ""
    if fields.get("status_json"):
        status_json_section = f"## Status JSON\n\n```json\n{fields['status_json']}\n```\n"

    prompt = VALIDATION_PROMPT.format(
        manufacturer=fields.get("manufacturer", "_not provided_"),
        model=fields.get("model", "_not provided_"),
        vid_pid=fields.get("vid_pid", "_not provided_"),
        firmware=fields.get("firmware", "_not provided_"),
        powers_device=fields.get("powers_device", "_not provided_"),
        status_reads=fields.get("status_reads", "_not provided_"),
        notes=fields.get("notes") or "None",
        serial_log=fields.get("serial_log") or "_No serial log provided_",
        status_json_section=status_json_section,
        max_lines=MAX_LOG_LINES,
    )

    print("[claude] Sending to Claude for analysis...")
    response = client.messages.create(
        model=CLAUDE_MODEL,
        max_tokens=1024,
        messages=[{"role": "user", "content": prompt}],
    )

    return response.content[0].text


# ── Output formatting ──────────────────────────────────────────────────────────

def print_report(issue: dict, fields: dict, verdict: str):
    """Print the full validation report to terminal."""
    width = 70
    line = "─" * width

    print(f"\n{'═' * width}")
    print(f"  UPS COMPATIBILITY VALIDATION REPORT")
    print(f"{'═' * width}")
    print(f"  Issue:      #{issue['number']} — {issue['title']}")
    print(f"  Submitted:  {issue['created_at'][:10]}  by @{issue['user']['login']}")
    print(f"  URL:        {issue['html_url']}")
    print(line)
    print(f"  Device:     {fields.get('manufacturer', '?')} {fields.get('model', '?')}")
    print(f"  VID:PID:    {fields.get('vid_pid', '?')}")
    print(f"  Firmware:   {fields.get('firmware', '?')}")
    print(f"  Log lines:  {len(fields.get('serial_log', '').splitlines())}")
    print(f"{'═' * width}\n")

    print("CLAUDE ANALYSIS")
    print(line)
    print(verdict)
    print(line)

    # Extract just the verdict line for a quick summary
    verdict_match = re.search(r"VERDICT:\s*(\S+)", verdict)
    if verdict_match:
        v = verdict_match.group(1).upper()
        icons = {
            "CONFIRMED":       "✅  CONFIRMED — apply 'confirmed' label",
            "PARTIAL":         "⚠️   PARTIAL — review concerns before confirming",
            "NEEDS-MORE-INFO": "❓  NEEDS MORE INFO — ask submitter for details",
            "NOT-SUPPORTED":   "❌  NOT SUPPORTED — apply 'not-supported' label and close",
        }
        print(f"\n  QUICK ACTION: {icons.get(v, v)}")
        print(f"  Issue URL: {issue['html_url']}\n")


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nUsage: python validate_submission.py <issue_number>")
        sys.exit(1)

    try:
        issue_number = int(sys.argv[1])
    except ValueError:
        print(f"❌  Invalid issue number: {sys.argv[1]}")
        sys.exit(1)

    check_env()

    print(f"[github] Fetching issue #{issue_number} from {GITHUB_REPO}...")
    issue = fetch_issue(issue_number)

    # Only process compatibility reports
    labels = [l["name"] for l in issue.get("labels", [])]
    if "compatibility-report" not in labels:
        print(f"⚠️   Issue #{issue_number} is not labeled 'compatibility-report'.")
        print(f"    Labels: {labels}")
        print("    Proceeding anyway...\n")

    print("[parse] Extracting submission fields...")
    fields = parse_issue_fields(issue["body"] or "")

    if not fields.get("serial_log"):
        print("⚠️   No serial log found in issue body.")
        print("    The analysis will be based on the submission fields only.\n")

    verdict = validate_with_claude(fields, issue_number, issue["title"])
    print_report(issue, fields, verdict)


if __name__ == "__main__":
    main()
