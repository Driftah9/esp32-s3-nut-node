# apply_v15_14_rid07_fix.ps1
# Updates rid=0x07 decode in decode_apc_smartups_direct()
# from stub to confirmed bit mapping (issue #1 on-battery log confirmed).
#
# Confirmed decode:
#   On-AC:      07 0C 00  -> byte0=0x0C -> bit2=1 (AC present), bit1=0
#   On-battery: 07 0A 00  -> byte0=0x0A -> bit2=0 (AC absent),  bit1=1 (discharging)
#   bit3 (0x08) always set - ignore
#   bit2 (0x04) = AC present
#   bit1 (0x02) = discharging
#
# Run from: D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\
# Usage: .\apply_v15_14_rid07_fix.ps1

Set-Location "D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node"
$file = "src\current\main\ups_hid_parser.c"

Write-Host "=== v15.14 rid=0x07 confirmed decode ===" -ForegroundColor Cyan
Write-Host "File: $file" -ForegroundColor Gray

$content = Get-Content $file -Raw -Encoding UTF8

# ---- old stub ----
$old = @'
    case 0x07:
        /* Status flags - on-battery bit mapping TBD pending issue #1 follow-up */
        ESP_LOGD(TAG, "[SMRT] rid=0x07 flags=0x%02X (decode pending)", plen >= 1 ? p[0] : 0xFF);
        break;
'@

# ---- new confirmed decode - written to temp file to avoid PS operator parse errors ----
$cNew = @'
    case 0x07:
        /*
         * Status flags byte - confirmed from issue #1 on-battery log.
         *
         * Observed values:
         *   0x0C (0000 1100) = on AC power       (bit3 + bit2)
         *   0x0A (0000 1010) = on battery/discharging (bit3 + bit1)
         *
         * Bit map:
         *   bit3 (0x08) = always set, purpose unknown - ignore
         *   bit2 (0x04) = AC present (1=AC, 0=on battery)
         *   bit1 (0x02) = discharging (1=discharging, 0=not discharging)
         *   bit0 (0x01) = always clear in observed data - ignore
         *
         * rid=0x07 alone provides full OL/OB status - GET_REPORT rid=0x06
         * charging/discharging flags are redundant for status but kept
         * for completeness (still used for CHRG flag detection).
         */
        if (plen >= 1) {
            bool ac_present  = (p[0] & 0x04u) != 0u;
            bool discharging = (p[0] & 0x02u) != 0u;
            upd->input_utility_present_valid = true;
            upd->input_utility_present       = ac_present;
            if (discharging) upd->ups_flags |= 0x02u;
            upd->ups_flags_valid = true;
            changed = true;
            ESP_LOGI(TAG, "[SMRT] rid=0x07 flags=0x%02X ac=%u discharging=%u",
                     p[0], (unsigned)ac_present, (unsigned)discharging);
        }
        break;
'@

Set-Content "$env:TEMP\rid07_new.txt" $cNew -Encoding UTF8 -NoNewline
$new = Get-Content "$env:TEMP\rid07_new.txt" -Raw -Encoding UTF8
Remove-Item "$env:TEMP\rid07_new.txt" -ErrorAction SilentlyContinue

if ($content.Contains($old)) {
    $content = $content.Replace($old, $new)
    Set-Content $file $content -Encoding UTF8 -NoNewline
    (Get-Item $file).LastWriteTime = Get-Date
    Write-Host "  OK - rid=0x07 confirmed decode applied" -ForegroundColor Green
} else {
    Write-Host "  SKIP - stub pattern not found (already applied?)" -ForegroundColor Yellow
}

# ---- also update VERSION HISTORY comment ----
$oldHist = @'
 v15.14 Add DECODE_APC_SMARTUPS for APC Smart-UPS C / Smart-UPS (PID 0003).
        decode_apc_smartups_direct(): rid=0x0D runtime (uint16-LE seconds),
        rid=0x07 flags stub (on-battery decode pending issue #1 follow-up).
        Charge from rid=0x0C handled by existing standard descriptor path.
'@

$newHist = @'
 v15.14 Add DECODE_APC_SMARTUPS for APC Smart-UPS C / Smart-UPS (PID 0003).
        decode_apc_smartups_direct(): rid=0x0D runtime (uint16-LE seconds),
        rid=0x07 status flags (confirmed: bit2=AC present, bit1=discharging),
        rid=0x0C charge via standard descriptor path.
        Confirmed from issue #1 Smart-UPS C 1500 on-battery discharge log.
'@

if ($content.Contains($oldHist)) {
    $content = $content.Replace($oldHist, $newHist)
    Set-Content $file $content -Encoding UTF8 -NoNewline
    (Get-Item $file).LastWriteTime = Get-Date
    Write-Host "  OK - VERSION HISTORY updated" -ForegroundColor Green
} else {
    Write-Host "  SKIP - version history pattern not found" -ForegroundColor Yellow
}

# ---- also update the function comment block above decode_apc_smartups_direct ----
$oldComment = @'
 * rid=0x07  byte[0] = status/flags byte
 *           Observed: 0x0C while on AC at full charge.
 *           On-battery decode pending - need discharge log from reporter.
 *
 * rid=0x0C  charge - handled by standard descriptor path (type=0 Input).
'@

$newComment = @'
 * rid=0x07  byte[0] = status flags (confirmed from issue #1 discharge log)
 *           0x0C (on AC):      bit2=1 AC present, bit1=0
 *           0x0A (on battery): bit2=0 AC absent,  bit1=1 discharging
 *           bit3 (0x08) always set - ignore. bit2=AC present, bit1=discharging.
 *
 * rid=0x0C  charge - handled by standard descriptor path (type=0 Input).
'@

if ($content.Contains($oldComment)) {
    $content = $content.Replace($oldComment, $newComment)
    Set-Content $file $content -Encoding UTF8 -NoNewline
    (Get-Item $file).LastWriteTime = Get-Date
    Write-Host "  OK - function header comment updated" -ForegroundColor Green
} else {
    Write-Host "  SKIP - function header comment not found" -ForegroundColor Yellow
}

Write-Host "`n=== Done ===" -ForegroundColor Cyan
Write-Host "Verify: git diff src/current/main/ups_hid_parser.c" -ForegroundColor Gray
Write-Host "Build:  idf.py build  (in ESP-IDF shell from src\current\)" -ForegroundColor Gray
