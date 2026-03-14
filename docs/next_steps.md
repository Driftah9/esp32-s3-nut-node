# Next Steps — esp32-s3-nut-node

## Cold-Start Resume Instructions (Session 16)

Read project_state.md first — it has the full bug description and exact fix.

---

## Immediate Next Action

**Fix AJAX auto-refresh in http_portal.c**

The portal does not auto-refresh. Root cause: malformed JS quote escaping in the
`doPoll()` callback causes silent JS failure. Fix is two PowerShell replacements.

### Step 1 — Read http_portal.c fresh to confirm strings before replacing

Claude should read the file and find the exact current strings before running
PowerShell replacements. The two target locations are:

**Location 1 — inside doPoll() JS callback (around line 390-400):**
Look for: `sc.innerHTML=` — this is the broken status update line

**Location 2 — inside render_dashboard snprintf (around line 340-350):**
Look for: `td_status` — this is the static HTML initial render

### Step 2 — Apply fix via PowerShell

```powershell
$file = "D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current\main\http_portal.c"
$content = Get-Content $file -Raw -Encoding UTF8

# Fix 1: JS callback — replace innerHTML with className+textContent
$content = $content -replace [regex]::Escape("sc.innerHTML='<span class=\\''+stCls(d.ups_status)+'\\'>'+d.ups_status+'</span>';"), "sc.className=stCls(d.ups_status);sc.textContent=d.ups_status;"

# Fix 2: Static HTML — remove span wrapper from td_status
$content = $content -replace [regex]::Escape("<tr><th>Status</th><td id='td_status'><span class='%s'>%s</span></td></tr>"), "<tr><th>Status</th><td id='td_status' class='%s'>%s</td></tr>"

Set-Content $file $content -Encoding UTF8 -NoNewline
Write-Host "Done"
```

### Step 3 — Build and flash

```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
idf.py build flash -p COM3 monitor
```

### Step 4 — Validate

- Open portal in Firefox at http://10.0.0.190
- Watch the `td_poll` clock tick (bottom of table — "Just updated", then "5s ago", etc.)
- Unplug UPS — status should change to OB within ~10s WITHOUT manual page refresh
- Plug back in — status should return to OL within ~10s WITHOUT manual page refresh

### Step 5 — Push to GitHub

```powershell
cd D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node
.\git-push.ps1
```

---

## After AJAX Fix — Remaining Tasks

| Task | Priority | Notes |
|------|----------|-------|
| GitHub push v15.10 | High | After AJAX fix confirmed |
| AP password | Medium | http://10.0.0.190/config → AP Password (8+ chars) |
| Z2M TCP keepalive | Medium | TubeZB coordinator web UI, 30-60s |
| Linux NUT hub (M9) | Low | Static IP for linuxtest LXC not assigned yet |
| Context warning in prefs | Low | Add to Claude_communication_preference.md |
| git tag v15.10 | Low | Removes -dirty from app version string |
