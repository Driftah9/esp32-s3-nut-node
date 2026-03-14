# Project State — [Project Name]

> This file is the shared memory bridge between Claude Desktop and Claude CLI.
> CLI updates this after every command run.
> Desktop reads this at the start of every session to restore full context.
> Last write wins — always overwrite, never append old entries.

---

## Last Updated
Date:
Updated by: [Desktop / CLI]

---

## Current Status

| Field | Value |
|-------|-------|
| Project Version | |
| Overall Status | [e.g. 🟢 Working / 🟡 In Progress / 🔴 Broken] |
| Last Action | |
| Last Action Result | [Success / Failed / Partial] |

---

## Last CLI Run

### Command
```
[command that was run]
```

### Result
```
[output summary — errors, warnings, key lines]
```

### Outcome
- [ ] Build passed
- [ ] Flash successful
- [ ] Monitor captured
- [ ] Tests passed
- [ ] Deploy successful

---

## Current Known Issues

| Issue | Severity | Status |
|-------|----------|--------|
| | | |

---

## Next Recommended Step

```
[exact next action — specific enough for a cold-start session to proceed immediately]
```

---

## Monitor Log Summary
*(ESP32 projects only — updated after each timed monitor run)*

Last monitor duration: [X seconds]
Log file: `docs\monitor.log`

Key observations:
-

Recommended next duration: [X seconds]
