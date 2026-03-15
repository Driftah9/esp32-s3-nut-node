# apply_v15_14_changes.ps1
# Applies all code changes for v15.14 Smart-UPS C 1500 support
# Run from: D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\
# Usage: .\apply_v15_14_changes.ps1

Set-Location "D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node"

$mainDir = "src\current\main"
$tmpDir  = $env:TEMP

Write-Host "=== v15.14 Smart-UPS C 1500 (PID 0003) support ===" -ForegroundColor Cyan

# ============================================================
# HELPER
# ============================================================
function Apply-Replace {
    param($file, $old, $new, $label)
    $content = Get-Content $file -Raw -Encoding UTF8
    if ($content.Contains($old)) {
        $content = $content.Replace($old, $new)
        Set-Content $file $content -Encoding UTF8 -NoNewline
        (Get-Item $file).LastWriteTime = Get-Date
        Write-Host "  OK - $label" -ForegroundColor Green
        return $true
    } else {
        Write-Host "  SKIP - $label (pattern not found, already applied?)" -ForegroundColor Yellow
        return $false
    }
}

# ============================================================
# 1. ups_device_db.h - add DECODE_APC_SMARTUPS mode
# ============================================================
Write-Host "`n[1/6] ups_device_db.h - add DECODE_APC_SMARTUPS..." -ForegroundColor Yellow

$old = @'
typedef enum {
    DECODE_STANDARD    = 0,  /* Generic HID descriptor path (default)        */
    DECODE_CYBERPOWER  = 1,  /* CyberPower direct-decode bypass               */
    DECODE_APC_BACKUPS = 2,  /* APC Back-UPS direct-decode bypass             */
} ups_decode_mode_t;
'@

$new = @'
typedef enum {
    DECODE_STANDARD     = 0,  /* Generic HID descriptor path (default)        */
    DECODE_CYBERPOWER   = 1,  /* CyberPower direct-decode bypass               */
    DECODE_APC_BACKUPS  = 2,  /* APC Back-UPS (PID 0002) direct-decode bypass  */
    DECODE_APC_SMARTUPS = 3,  /* APC Smart-UPS (PID 0003) direct-decode bypass */
} ups_decode_mode_t;
'@

Apply-Replace "$mainDir\ups_device_db.h" $old $new "DECODE_APC_SMARTUPS enum value"

# ============================================================
# 2. ups_device_db.c - update PID 0003 entry
# ============================================================
Write-Host "`n[2/6] ups_device_db.c - update PID 0003 entry..." -ForegroundColor Yellow

$old = @'
    {
        /* APC Smart-UPS C / Smart-UPS (PID 0x0003) -- standard HID path
         * Descriptor has runtime at uid=0x0085, charge at rid=0x0C,
         * charging at uid=0x008B, discharging at uid=0x002C.
         * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s,
         * charge.low=10%, charge.warning=50%.
         * Status: needs-validation (issue #1, Smart-UPS C 1500). */
        .vid         = 0x051D,
        .pid         = 0x0003,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS C / Smart-UPS (PID 0003)",
        .decode_mode = DECODE_STANDARD,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
'@

$new = @'
    {
        /* APC Smart-UPS C / Smart-UPS (PID 0x0003)
         * Confirmed: Smart-UPS C 1500 (issue #1, v15.13 log).
         *
         * Interrupt-IN delivers three undocumented rids not in descriptor:
         *   rid=0x0C  byte[0] = battery.charge % (also Input in descriptor)
         *   rid=0x0D  byte[0:1] uint16-LE = battery.runtime seconds
         *   rid=0x07  byte[0] = status flags (on-battery decode pending)
         *
         * Feature reports (GET_REPORT) for remaining vars:
         *   rid=0x06  byte[1]=charging (uid 008B), byte[2]=discharging (uid 002C)
         *   rid=0x0E  byte[1]=battery.voltage (uid 0083, raw value 0-100)
         *
         * NUT DDL: battery.voltage.nominal=24V, runtime.low=120s. */
        .vid         = 0x051D,
        .pid         = 0x0003,
        .vendor_name = "APC",
        .model_hint  = "Smart-UPS C / Smart-UPS (PID 0003)",
        .decode_mode = DECODE_APC_SMARTUPS,
        .quirks      = QUIRK_VENDOR_PAGE_REMAP | QUIRK_NEEDS_GET_REPORT,
        .known_good  = false,
        .battery_voltage_nominal_mv = 24000,
        .battery_runtime_low_s      = 120,
        .battery_charge_low         = 10,
        .battery_charge_warning     = 50,
        .input_voltage_nominal_v    = 120,
        .ups_type                   = "line-interactive",
    },
'@

Apply-Replace "$mainDir\ups_device_db.c" $old $new "PID 0003 decode_mode and quirks"

# ============================================================
# 3. ups_hid_parser.c - add Smart-UPS direct-decode
# ============================================================
Write-Host "`n[3/6] ups_hid_parser.c - add Smart-UPS decode..." -ForegroundColor Yellow

# 3a - mode log
$old = @'
    if (use_direct) {
        ESP_LOGI(TAG, "Decode mode: DIRECT (CyberPower bypass active)");
    } else if (s_device && s_device->decode_mode == DECODE_APC_BACKUPS) {
        ESP_LOGI(TAG, "Decode mode: APC Back-UPS (direct + standard combined)");
    } else {
        ESP_LOGI(TAG, "Decode mode: STANDARD (generic HID descriptor path)");
    }
'@

$new = @'
    if (use_direct) {
        ESP_LOGI(TAG, "Decode mode: DIRECT (CyberPower bypass active)");
    } else if (s_device && s_device->decode_mode == DECODE_APC_BACKUPS) {
        ESP_LOGI(TAG, "Decode mode: APC Back-UPS (direct + standard combined)");
    } else if (s_device && s_device->decode_mode == DECODE_APC_SMARTUPS) {
        ESP_LOGI(TAG, "Decode mode: APC Smart-UPS (direct INT-IN + GET_REPORT)");
    } else {
        ESP_LOGI(TAG, "Decode mode: STANDARD (generic HID descriptor path)");
    }
'@

Apply-Replace "$mainDir\ups_hid_parser.c" $old $new "decode mode log for Smart-UPS"

# 3b - new decode function - write C code to temp file to avoid PS parser issues
$cCode3b = @'
/* ---- APC Smart-UPS direct-decode ------------------------------------ */
/*
 * APC Smart-UPS C / Smart-UPS (PID 0x0003) live interrupt-IN rid map.
 * Confirmed from issue #1 (Smart-UPS C 1500, v15.13 log).
 *
 * All rids below are NOT declared in the HID descriptor - they arrive
 * as undocumented interrupt-IN reports alongside the descriptor-declared
 * rid=0x0C charge report.
 *
 * rid=0x0D  byte[0:1] = uint16 LE = battery.runtime seconds
 *           Confirmed: 0x1194=4500s, 0x120C=4620s at 100pct charge.
 *           Value oscillates as UPS recalculates remaining runtime.
 *
 * rid=0x07  byte[0] = status/flags byte
 *           Observed: 0x0C while on AC at full charge.
 *           On-battery decode pending - need discharge log from reporter.
 *
 * rid=0x0C  charge - handled by standard descriptor path (type=0 Input).
 */
static bool decode_apc_smartups_direct(uint8_t rid,
                                        const uint8_t *p, size_t plen,
                                        ups_state_update_t *upd)
{
    bool changed = false;
    switch (rid) {
    case 0x0D:
        if (plen >= 2) {
            uint16_t runtime_s = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            if (runtime_s > 0 && runtime_s < 65000u) {
                upd->battery_runtime_valid = true;
                upd->battery_runtime_s     = runtime_s;
                changed = true;
                ESP_LOGI(TAG, "[SMRT] battery.runtime=%us (rid=0x0D)", runtime_s);
            }
        }
        break;
    case 0x07:
        /* Status flags - on-battery bit mapping TBD pending issue #1 follow-up */
        ESP_LOGD(TAG, "[SMRT] rid=0x07 flags=0x%02X (decode pending)", plen >= 1 ? p[0] : 0xFF);
        break;
    case 0x0C:
        /* Charge handled by standard descriptor path - no action here */
        break;
    default:
        break;
    }
    return changed;
}

/* ---- derive_status --------------------------------------------------- */
'@

Set-Content "$tmpDir\patch_3b.txt" $cCode3b -Encoding UTF8 -NoNewline

$old = '/* ---- derive_status --------------------------------------------------- */'
$new = Get-Content "$tmpDir\patch_3b.txt" -Raw -Encoding UTF8
Apply-Replace "$mainDir\ups_hid_parser.c" $old $new "decode_apc_smartups_direct() function"

# 3c - dispatch
$old = @'
    if (mode == DECODE_CYBERPOWER) {
        if (decode_cyberpower_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        if (rid != 0x20) goto finalize;
    } else if (mode == DECODE_APC_BACKUPS) {
        /* APC Back-UPS: direct for vendor rids, then fall through to
           standard path to pick up charging/discharging from descriptor. */
        if (decode_apc_backups_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        /* Always fall through to standard path for descriptor fields. */
    }
'@

$new = @'
    if (mode == DECODE_CYBERPOWER) {
        if (decode_cyberpower_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        if (rid != 0x20) goto finalize;
    } else if (mode == DECODE_APC_BACKUPS) {
        /* APC Back-UPS: direct for vendor rids, then fall through to
           standard path to pick up charging/discharging from descriptor. */
        if (decode_apc_backups_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        /* Always fall through to standard path for descriptor fields. */
    } else if (mode == DECODE_APC_SMARTUPS) {
        /* APC Smart-UPS: direct for undocumented interrupt-IN rids,
           then fall through to standard path for descriptor charge field. */
        if (decode_apc_smartups_direct(rid, payload, payload_len, upd)) {
            changed = true;
        }
        /* Fall through - standard path picks up rid=0x0C charge (Input). */
    }
'@

Apply-Replace "$mainDir\ups_hid_parser.c" $old $new "decode_report dispatch for Smart-UPS"

# 3d - version history
$old = @'
 v15.7  Remove input_voltage and output_voltage from cache, standard
        decode path, and CyberPower direct-decode.
'@

$new = @'
 v15.7  Remove input_voltage and output_voltage from cache, standard
        decode path, and CyberPower direct-decode.
 v15.14 Add DECODE_APC_SMARTUPS for APC Smart-UPS C / Smart-UPS (PID 0003).
        decode_apc_smartups_direct(): rid=0x0D runtime (uint16-LE seconds),
        rid=0x07 flags stub (on-battery decode pending issue #1 follow-up).
        Charge from rid=0x0C handled by existing standard descriptor path.
'@

Apply-Replace "$mainDir\ups_hid_parser.c" $old $new "ups_hid_parser.c VERSION HISTORY"

# ============================================================
# 4. ups_get_report.c - add Smart-UPS rid list and decode
# ============================================================
Write-Host "`n[4/6] ups_get_report.c - add Smart-UPS GET_REPORT support..." -ForegroundColor Yellow

# 4a - rid list
$old = @'
static const uint8_t s_apc_rids[]       = { 0x17 };
static const size_t  s_apc_rids_n       = sizeof(s_apc_rids) / sizeof(s_apc_rids[0]);
static const uint8_t s_tripplite_rids[] = { 0x01, 0x0C };
static const size_t  s_tripplite_rids_n = sizeof(s_tripplite_rids) / sizeof(s_tripplite_rids[0]);
'@

$new = @'
static const uint8_t s_apc_rids[]          = { 0x17 };
static const size_t  s_apc_rids_n          = sizeof(s_apc_rids) / sizeof(s_apc_rids[0]);
/* APC Smart-UPS (PID 0003) Feature rids:
 *   rid=0x06  charging flag (byte[1]) + discharging flag (byte[2])
 *   rid=0x0E  battery.voltage (byte[1], raw value)
 * Runtime arrives on interrupt-IN (rid=0x0D) so no GET_REPORT needed. */
static const uint8_t s_apc_smartups_rids[] = { 0x06, 0x0E };
static const size_t  s_apc_smartups_rids_n = sizeof(s_apc_smartups_rids) / sizeof(s_apc_smartups_rids[0]);
static const uint8_t s_tripplite_rids[]    = { 0x01, 0x0C };
static const size_t  s_tripplite_rids_n    = sizeof(s_tripplite_rids) / sizeof(s_tripplite_rids[0]);
'@

Apply-Replace "$mainDir\ups_get_report.c" $old $new "Smart-UPS rid list"

# 4b - decode function - write to temp file
$cCode4b = @'
/* ---- Decode APC Smart-UPS Feature reports ----------------------------- */
/*
 * APC Smart-UPS C / Smart-UPS (PID 0003) Feature report decode.
 * GET_REPORT response: data[0] = report ID echo, data[1..N] = payload.
 *
 * rid=0x06  [rid, charging_byte, discharging_byte]
 *   data[1] = uid=008B charging flag
 *   data[2] = uid=002C discharging flag
 *
 * rid=0x0E  [rid, voltage_raw, charge_backup]
 *   data[1] = uid=0083 battery voltage (raw, scale TBD from issue #1 log)
 */
static void decode_apc_smartups_feature(uint8_t rid, const uint8_t *data, size_t len)
{
    char hexbuf[48] = {0};
    int  pos = 0;
    size_t n = (len > 8u) ? 8u : len;
    for (size_t i = 0; i < n; i++) {
        pos += snprintf(hexbuf + pos, sizeof(hexbuf) - (size_t)pos,
                        "%02X%s", data[i], (i == n-1u) ? "" : " ");
    }
    ESP_LOGI(TAG, "[SMRT Feature] rid=0x%02X len=%u: %s", rid, (unsigned)len, hexbuf);

    switch (rid) {
    case 0x06: {
        if (len < 3u) {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x06: short read %u bytes", (unsigned)len);
            break;
        }
        bool charging    = (data[1] != 0u);
        bool discharging = (data[2] != 0u);
        ESP_LOGI(TAG, "[SMRT Feature] rid=0x06 charging=%u discharging=%u",
                 (unsigned)charging, (unsigned)discharging);
        ups_state_update_t upd;
        memset(&upd, 0, sizeof(upd));
        upd.valid           = true;
        upd.ups_flags_valid = true;
        if (charging)    upd.ups_flags |= 0x01u;
        if (discharging) upd.ups_flags |= 0x02u;
        upd.input_utility_present_valid = true;
        upd.input_utility_present       = !discharging;
        ups_state_apply_update(&upd);
        break;
    }
    case 0x0E: {
        if (len < 2u) {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x0E: short read %u bytes", (unsigned)len);
            break;
        }
        uint8_t raw = data[1];
        /* raw = whole volts (tentative). Sanity: 8..60V for 12/24/48V systems. */
        if (raw >= 8u && raw <= 60u) {
            ups_state_update_t upd;
            memset(&upd, 0, sizeof(upd));
            upd.valid                 = true;
            upd.battery_voltage_valid = true;
            upd.battery_voltage_mv    = (uint32_t)raw * 1000u;
            ups_state_apply_update(&upd);
            ESP_LOGI(TAG, "[SMRT Feature] battery.voltage=%uV", (unsigned)raw);
        } else {
            ESP_LOGW(TAG, "[SMRT Feature] rid=0x0E raw=%u outside 8-60V - ignoring", (unsigned)raw);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- Timer task — only posts to queue, does NO USB work -------------- */
'@

Set-Content "$tmpDir\patch_4b.txt" $cCode4b -Encoding UTF8 -NoNewline

$old = '/* ---- Timer task — only posts to queue, does NO USB work -------------- */'
$new = Get-Content "$tmpDir\patch_4b.txt" -Raw -Encoding UTF8
Apply-Replace "$mainDir\ups_get_report.c" $old $new "decode_apc_smartups_feature() function"

# 4c - timer task routing
$old = @'
    if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
        rids   = s_apc_rids;
        rids_n = s_apc_rids_n;
    } else {
        rids   = s_tripplite_rids;
        rids_n = s_tripplite_rids_n;
    }
'@

$new = @'
    if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
        rids   = s_apc_rids;
        rids_n = s_apc_rids_n;
    } else if (s_entry && s_entry->decode_mode == DECODE_APC_SMARTUPS) {
        rids   = s_apc_smartups_rids;
        rids_n = s_apc_smartups_rids_n;
    } else {
        rids   = s_tripplite_rids;
        rids_n = s_tripplite_rids_n;
    }
'@

Apply-Replace "$mainDir\ups_get_report.c" $old $new "timer task routing for Smart-UPS"

# 4d - service_queue routing
$old = @'
    if (err == ESP_OK && got > 0) {
        if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
            decode_apc_feature(req.rid, buf, got);
        }
        /* Future: add Tripp Lite decode branch here */
    }
'@

$new = @'
    if (err == ESP_OK && got > 0) {
        if (s_entry && s_entry->decode_mode == DECODE_APC_BACKUPS) {
            decode_apc_feature(req.rid, buf, got);
        } else if (s_entry && s_entry->decode_mode == DECODE_APC_SMARTUPS) {
            decode_apc_smartups_feature(req.rid, buf, got);
        }
        /* Future: add Tripp Lite decode branch here */
    }
'@

Apply-Replace "$mainDir\ups_get_report.c" $old $new "service_queue decode routing"

# 4e - version history
$old = @'
 R1  v15.8  Rewrite -- single-owner queue pattern to fix USB concurrency.
'@

$new = @'
 R1  v15.8  Rewrite -- single-owner queue pattern to fix USB concurrency.
 R2  v15.14 Add APC Smart-UPS (PID 0003) GET_REPORT support.
            s_apc_smartups_rids[] = {0x06, 0x0E}
            decode_apc_smartups_feature(): charging/discharging from rid=0x06,
            battery.voltage from rid=0x0E.
'@

Apply-Replace "$mainDir\ups_get_report.c" $old $new "ups_get_report.c VERSION HISTORY"

# ============================================================
# 5. nut_server.c - bump DRIVER_VERSION to 15.14
# ============================================================
Write-Host "`n[5/6] nut_server.c - bump version to 15.14..." -ForegroundColor Yellow

$old = 'static const char *DRIVER_VERSION = "15.12";'
$new = 'static const char *DRIVER_VERSION = "15.14";'
Apply-Replace "$mainDir\nut_server.c" $old $new "DRIVER_VERSION 15.12 -> 15.14"

# ============================================================
# 6. wifi_mgr.c - fix ip4addr_aton for IDF v5.5+ compatibility
# ============================================================
Write-Host "`n[6/6] wifi_mgr.c - fix ip4addr_aton pointer type..." -ForegroundColor Yellow

$file = "$mainDir\wifi_mgr.c"
$content = Get-Content $file -Raw -Encoding UTF8

# Try both line endings
$old_crlf = "    ip4addr_aton(WIFI_MGR_SOFTAP_IP_STR, &ip.ip);`r`n    ip4addr_aton(WIFI_MGR_SOFTAP_GW_STR, &ip.gw);`r`n    ip4addr_aton(WIFI_MGR_SOFTAP_NETMASK_STR, &ip.netmask);"
$old_lf   = "    ip4addr_aton(WIFI_MGR_SOFTAP_IP_STR, &ip.ip);`n    ip4addr_aton(WIFI_MGR_SOFTAP_GW_STR, &ip.gw);`n    ip4addr_aton(WIFI_MGR_SOFTAP_NETMASK_STR, &ip.netmask);"
$new6     = "    ip4addr_aton(WIFI_MGR_SOFTAP_IP_STR,     (ip4_addr_t *)&ip.ip);`n    ip4addr_aton(WIFI_MGR_SOFTAP_GW_STR,      (ip4_addr_t *)&ip.gw);`n    ip4addr_aton(WIFI_MGR_SOFTAP_NETMASK_STR, (ip4_addr_t *)&ip.netmask);"

if ($content.Contains($old_crlf)) {
    $content = $content.Replace($old_crlf, $new6)
    Set-Content $file $content -Encoding UTF8 -NoNewline
    (Get-Item $file).LastWriteTime = Get-Date
    Write-Host "  OK - ip4addr_aton casts added" -ForegroundColor Green
} elseif ($content.Contains($old_lf)) {
    $content = $content.Replace($old_lf, $new6)
    Set-Content $file $content -Encoding UTF8 -NoNewline
    (Get-Item $file).LastWriteTime = Get-Date
    Write-Host "  OK - ip4addr_aton casts added" -ForegroundColor Green
} else {
    Write-Host "  SKIP - pattern not found (already applied?)" -ForegroundColor Yellow
}

# ============================================================
# CLEANUP temp files
# ============================================================
Remove-Item "$tmpDir\patch_3b.txt" -ErrorAction SilentlyContinue
Remove-Item "$tmpDir\patch_4b.txt" -ErrorAction SilentlyContinue

Write-Host "`n=== All done ===" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor White
Write-Host "  git rm --cached src/current/sdkconfig.defaults 2>`$null" -ForegroundColor Gray
Write-Host "  git rm --cached src/current/sdkconfig 2>`$null" -ForegroundColor Gray
Write-Host "  git diff src/current/main/" -ForegroundColor Gray
Write-Host "  idf.py build  (in ESP-IDF shell from src\current\)" -ForegroundColor Gray
