# GitHub Push — esp32-s3-nut-node

> Claude updates this file whenever code changes during a session.
> The git-push.ps1 script reads this file for all push details.
> Keep this current — it is the single source of truth for every push.

---

## Project
esp32-s3-nut-node

## Repo
https://github.com/Driftah9/esp32-s3-nut-node

## Visibility
public

## Branch
main

## Version
v15.9

## Commit Message
- Add .github/ISSUE_TEMPLATE/ups-compatibility-report.yml - structured UPS submission form
- Add .github/ISSUE_TEMPLATE/bug_report.yml - bug report form
- Add .github/workflows/label-new-issue.yml - auto-labels compatibility reports, posts checklist
- Add .github/workflows/update-compat-list.yml - auto-updates confirmed-ups.md on confirmed label
- Add docs/confirmed-ups.md - seeded with 3 confirmed devices, auto-updated by workflow
- Add scripts/validate_submission.py - local validation tool using Claude API
- Update docs/TODO.md - Phase 2 Zigbee opt-in design, Z8/Z9/Z10 added
- Fix git-push.ps1 - correctly stages project-root files for ESP32 projects
