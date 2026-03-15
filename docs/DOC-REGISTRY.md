# DOC-REGISTRY — esp32-s3-nut-node
# All files tracked in this registry are part of the GitHub repo.
# Claude must read this file at the start of every push preparation
# and update every applicable file before writing github_push.md.

---

## How This Works

Before every `git-push.ps1` run, Claude:
1. Reads this registry
2. Checks each tracked file against the change type (see Change Types below)
3. Updates every file marked as required for that change type
4. Writes `github_push.md` only after all required doc updates are complete
5. Includes updated doc files in the commit

**No exceptions. If a doc is stale, update it before pushing.**

---

## Change Types

| Code | Meaning | Example |
|------|---------|---------|
| `FEAT` | New feature added | lightbox, hotplug fix, new NUT variable |
| `FIX` | Bug fix | format specifier, buffer size |
| `REFACTOR` | Code restructure, no behavior change | module split |
| `DEVICE` | New device added or confirmed | new VID:PID, community report |
| `DOC` | Documentation only | updating stale text, typo fix |
| `INFRA` | Build, CI, scripts, workflow | CLAUDE.md rule, .gitignore |

---

## Tracked Files

### README.md
- **Purpose:** Public-facing project overview on GitHub
- **Location:** project root
- **Update on:** `FEAT` `DEVICE` — always
- **Update on:** `FIX` `REFACTOR` — only if user-visible behavior changes
- **Skip on:** `DOC` `INFRA`
- **What to update:**
  - Version number in header
  - Features list
  - NUT Variables table
  - Source file list
  - Confirmed devices table (firmware version)

---

### docs/HISTORY.md
- **Purpose:** Chronological development narrative — permanent record
- **Location:** docs/HISTORY.md
- **Update on:** `FEAT` `FIX` (significant) — always
- **Skip on:** `DOC` `INFRA` `FIX` (minor/cosmetic)
- **What to update:**
  - Add new version section under the current phase
  - Update "Current Version" in header
  - Update "Confirmed Working Devices" table version
- **Note:** HISTORY.md is append-only. Never edit past entries.

---

### docs/MILESTONES.md
- **Purpose:** Milestone tracker — what was planned, what shipped
- **Location:** docs/MILESTONES.md
- **Update on:** `FEAT` — always (mark completed, add next)
- **Skip on:** `FIX` `REFACTOR` `DOC` `INFRA`
- **What to update:**
  - Mark completed milestones ✅ Confirmed with version + date
  - Add new "Next Milestones" entries for upcoming work
  - Move completed items out of "Next" into "Completed"

---

### docs/DECISIONS.md
- **Purpose:** Architectural decision log — why we built it this way
- **Location:** docs/DECISIONS.md
- **Update on:** Any change that involved a meaningful design choice
- **Skip on:** `FIX` (trivial), `DOC`, `INFRA`
- **What to update:**
  - Add new DXXX entry with Decision + Reason + Version confirmed
- **Note:** DECISIONS.md is append-only. Never edit past decisions.

---

### docs/TODO.md
- **Purpose:** Task tracker — what is done, what is pending
- **Location:** docs/TODO.md
- **Update on:** Every push — always
- **What to update:**
  - Mark completed tasks ✅ Done with version
  - Add new pending tasks discovered during work
  - Update "Last updated" date and version at top

---

### docs/REVERT-INDEX.md
- **Purpose:** Known-good firmware anchors for safe rollback
- **Location:** docs/REVERT-INDEX.md
- **Update on:** Every confirmed flash — always
- **Skip on:** `DOC` `INFRA` (no firmware change)
- **What to update:**
  - Add new REVERT-XXXX entry with version, date, what works, binary size, IRAM%
- **Note:** REVERT-INDEX.md is append-only. Never edit past anchors.

---

### docs/COMPATIBLE-UPS.md
- **Purpose:** Quick-reference compatibility table for users
- **Location:** docs/COMPATIBLE-UPS.md
- **Update on:** `FEAT` `DEVICE` — always
- **Skip on:** `FIX` `REFACTOR` `DOC` `INFRA`
- **What to update:**
  - Firmware version in header
  - NUT Variables Served table
  - Notes — fix any stale "pending" references
  - Add new expected/confirmed device entries

---

### docs/confirmed-ups.md
- **Purpose:** Community-confirmed device list (linked from GitHub issues)
- **Location:** docs/confirmed-ups.md
- **Update on:** `FEAT` `DEVICE` — always
- **Skip on:** `FIX` `REFACTOR` `DOC` `INFRA`
- **What to update:**
  - Firmware version on all confirmed devices
  - Metrics Reference table — add new variables with source/notes
  - "Last manual seed" date at bottom

---

## Files That Are Historical — No Updates Needed

These files are frozen snapshots. Claude must NEVER modify them:

| File | Reason |
|------|--------|
| `docs/HID_REPORT_MAP.md` | Raw HID dump from specific hardware — historical record |
| `docs/esp32/` | Hardware reference docs — not version dependent |
| `docs/linux/` | Linux NUT reference — external, not our content |
| `docs/REVERT_GUIDE.md` | How-to guide — stable, not version dependent |

---

## Files That Are Reference — Update Only When Content Changes

| File | Update trigger |
|------|---------------|
| `docs/usbhid-ups-compat.md` | Only when new HID usage IDs or vendor quirks are discovered |
| `docs/DECISIONS.md` | Append only — new entries when decisions are made |

---

## Adding New Tracked Files

When a new doc file is added to the repo:
1. Add it to this registry with Purpose, Location, Update trigger, What to update
2. Classify it as: always-update / conditional-update / historical / reference
3. Claude reads this file at push time — new entries are automatically enforced

---

*Registry version: 1.0 | Created: 2026-03-15 | Project: esp32-s3-nut-node*
