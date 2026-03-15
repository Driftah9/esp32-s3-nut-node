# Next Steps — esp32-s3-nut-node

## Cold-Start Resume Instructions (Session 16)

---

## Immediate Next Action

**Two bugs to fix — CyberPower discharge data not updating correctly**

### Bug 1 — Charge and Runtime not dynamically updating on portal (AJAX issue)
- Charge updates when manually refreshing (73% → 72%) but NOT via AJAX auto-poll
- Runtime stays static at 5m 00s even after 10+ minutes on battery
- This suggests the AJAX `addOrUpdate()` calls for `tr_charge` and `tr_runtime` are
  silently failing — likely because `td_tr_charge` vs `td_charge` ID mismatch
- The `/status` JSON endpoint may be serving stale charge/runtime values

**Check 1:** Verify `/status` JSON actually returns changing charge/runtime values
while on battery — open `http://10.0.0.190/status` and watch it directly.

**Check 2:** Check the `addOrUpdate()` td ID convention — the function sets
`td.id = 'td_' + id` where id is e.g. `tr_charge`, making the td id `td_tr_charge`.
But the initial static HTML renders `<td id='td_charge'>`.
This mismatch means AJAX never finds the existing td and tries to insert a duplicate row.

**Fix:** In the AJAX `doPoll()` calls, change:
```javascript
addOrUpdate('tr_charge','Charge',d.battery_charge+'%');
addOrUpdate('tr_runtime','Runtime',rt);
addOrUpdate('tr_bvolt','Batt Voltage',...);
```
The `addOrUpdate` function uses `id` as both the `<tr>` id AND derives the `<td>` id
as `'td_'+id`. So for `tr_charge`, it looks for `<td id='td_tr_charge'>` but the
static HTML has `<td id='td_charge'>`. Need to align these — either fix the static
HTML or fix the addOrUpdate calls.

### Bug 2 — Runtime wrong source — CONFIRMED via log analysis + NUT source
- `rid=0x82` = `82 2C 01` = 0x012C = 300s STATIC — never changes, it is `battery.runtime.low` threshold
- `rid=0x21` IS the real runtime — 16-bit little-endian seconds, counts down correctly:
  - `21 84 0D` = 0x0D84 = 3460s ≈ 57 min (start of discharge)
  - `21 92 0A` = 0x0A92 = 2706s ≈ 45 min
  - `21 79 0A` = 0x0A79 = 2681s ≈ 44 min (near end of log)
- NUT cps-hid.c maps `battery.runtime` to `UPS.PowerSummary.RunTimeToEmpty` (HID usage 0x0068)
- All other PID=0501 CyberPower devices report real dynamic runtime via NUT
- Our firmware ignores rid=0x21 entirely — this is the fix

**Fix in `ups_hid_parser.c` CyberPower direct-decode section:**
- Add `case 0x21:` — decode bytes 1-2 as 16-bit LE runtime in seconds
- Map to `ups_state.battery_runtime_s`, set `battery_runtime_valid = true`
- Remove or ignore `rid=0x82` for runtime (it is the low-battery runtime threshold, not current runtime)

### Bug 3 — Occasional random OL flicker during OB (low priority)
- Very rare OL flicker during sustained OB discharge observed
- Already have debounce logic consideration from Session 14
- Low priority — happens rarely, not critical

---

## Log Analysis Needed
Monitor logs from extended discharge test are in:
`D:\Users\Stryder\Documents\Claude\PROJECT_UPLOAD\`
- monitor_20260314_024448.log
- monitor_20260314_024542.log
- monitor_20260314_024627.log
- monitor_20260314_024642.log
- monitor_20260314_024710.log (largest — ~8 min run, battery discharged from ~73%)

**Logs are UTF-16 encoded** — cannot be read directly by filesystem tool efficiently.
User should paste relevant sections or convert to UTF-8 first:
```powershell
Get-Content "D:\Users\Stryder\Documents\Claude\PROJECT_UPLOAD\monitor_20260314_024710.log" -Encoding Unicode | Set-Content "D:\Users\Stryder\Documents\Claude\PROJECT_UPLOAD\monitor_converted.log" -Encoding UTF8
```

**What to look for in logs:**
1. Any `rid=0x82` value that changes from `82 2C 01` (300s) — confirms runtime updates
2. Any `battery.charge` lines showing values below 100% and changing
3. Any `battery.voltage` lines — does it change during discharge?
4. Any OL flicker events (derive_status -> OL while supposed to be OB)

---

## Clock Fix (also needed)
Replace "5s ago / Just updated" age clock with live wall clock showing:
- Current time ticking every second: `Now: 2:19:25 AM`
- Last poll time: `Last poll: 2:19:20 AM`

### JS replacement needed in render_dashboard():

**Remove this block:**
```javascript
var lastOk=Date.now();
function fmtAge(){...}
var ck=setInterval(function(){
  document.getElementById('td_poll').textContent=fmtAge();
},1000);
```

**Replace with:**
```javascript
var lastOk=null;
function fmtTime(d){
  var h=d.getHours(),m=d.getMinutes(),s=d.getSeconds();
  var ap=h>=12?'PM':'AM';h=h%12||12;
  return h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s+' '+ap;
}
var ck=setInterval(function(){
  var el=document.getElementById('td_poll');
  var now=fmtTime(new Date());
  el.textContent=lastOk?('Now: '+now+' | Last poll: '+fmtTime(lastOk)):('Now: '+now+' | Polling...');
},1000);
```

**And change** `lastOk=Date.now()` → `lastOk=new Date()` in doPoll() success handler.

**Apply via PowerShell** using `.Replace()` method — see Claude_communication_preference.md.

---

### Item 4 — Audit runtime decoding for ALL known UPS devices
Confirm how runtime is decoded for each device in the device DB and validate correctness:

| Device | VID:PID | Runtime source | Current decode | Status |
|--------|---------|---------------|----------------|--------|
| CyberPower CP550HG | 0764:0501 | rid=0x21 (16-bit LE seconds) | rid=0x82 WRONG | Fix needed |
| APC Back-UPS XS 1500M | 051D:0002 | rid=0x0C bytes 3-4 (GET_REPORT) | Confirmed working | OK |
| APC Back-UPS BR1000G | 051D:0002 | same as XS 1500M | Confirmed working | OK |

For Session 16:
- Review `ups_hid_parser.c` decode path for every device in `ups_device_db.c`
- Confirm which rid/byte carries runtime for each VID:PID
- Cross-reference against NUT cps-hid.c and apc-hid.c source mappings
- Verify runtime_valid flag is set correctly and value is in seconds
- Check that battery_voltage_valid is also correctly set per device
- Document confirmed decode map in decisions_made.md

---

## Pending Tasks

| Task | Priority | Status |
|------|----------|---------|
| git tag v15.12 + push tag | Low | Pending — cleans dirty version string |
| Web portal NUT lightbox | Medium | Designed, not yet built |
| GitHub issue #1 comment | Medium | Point APC Smart-UPS C reporter to v15.12 |
| Phase 2: ups.load (CyberPower rid?) | Low | Pending — correct rid not identified |
| Phase 2: input.voltage live | Low | Pending — needs GET_REPORT path |
| Phase 2: output.frequency | Low | Pending — needs GET_REPORT path |
| IRAM headroom audit | Medium | At 99.99% — audit before next code addition |
| ups_hid_desc.c split | Low | At ~630 lines, split recommended |
| Occasional OL flicker during OB | Low | Rare, not critical |
