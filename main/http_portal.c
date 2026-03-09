/*============================================================================
 MODULE: http_portal

 RESPONSIBILITY
 - Raw-socket config portal
 - GET  /         -> status dashboard (UPS metrics)
 - GET  /config   -> configuration form
 - POST /save     -> save config to NVS
 - GET  /status   -> JSON snapshot (unauthenticated, for scripts)
 - GET  /reboot   -> reboot device

 REVERT HISTORY
 R0   v14.7  modular baseline
 R4   v14.25 rx/page buffers heap-allocated (stack overflow fix)
 R5   v14.25 Graceful socket close (Chrome ERR_CONNECTION_RESET fix)
 R8   v14.25 Zero-CSS plain HTML. HTTP_PAGE_BUF=4096.
 R9   v14.25 Default password warning via cfg_store_is_default_pass()
 R10  v14.25 AJAX live polling on dashboard. Small <script> polls /status
             JSON every 5s and updates table cells in-place. Polling only
             runs while the page is open (no background activity). Config
             page and /status endpoint unchanged.
 R11  v15.5  Version bump. Expanded /status JSON to include all live UPS
             metrics (runtime, voltages). AJAX now updates all live cells.
             Manufacturer/model shown regardless of ups.valid (set at USB
             enumeration, always available after first connect).
 R12  v15.6  Version string bump only (runtime decode in ups_hid_parser).
 R13  v15.7  Dynamic dashboard: always show Manufacturer/Model/Driver/Status.
             Optional rows (Charge/Runtime/Batt Voltage/Load) only rendered
             when valid. AJAX adds/removes rows dynamically per /status JSON.
             Remove input.voltage and output.voltage from portal entirely.
 R14  v15.8  Re-add input_voltage_v and output_voltage_v to /status JSON.
             AJAX adds tr_ivolt / tr_ovolt rows when Feature report data arrives.
             Version bump throughout.
 R15  v15.8  Add GET /compat — Compatible UPS device list page.
             Source of truth: ups_device_db.c. Three confirmed devices marked.

============================================================================*/

#include "http_portal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "cfg_store.h"
#include "wifi_mgr.h"
#include "ups_state.h"

static const char *TAG = "http_portal";

#define HTTP_PORT     80
#define HTTP_RX_MAX   4096
#define HTTP_BODY_MAX 2048
#define HTTP_PAGE_BUF 5120   /* bumped from 4096 — expanded JSON + JS */

/* -------------------------------------------------------------------------
 * String utilities
 * ---------------------------------------------------------------------- */

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

static bool is_printable_ascii(const char *s) {
    if (!s) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

static void url_decode_inplace(char *s) {
    if (!s) return;
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hh[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hh, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
}

static void trim_ws(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

/* -------------------------------------------------------------------------
 * Base64 encoder (for Basic Auth)
 * ---------------------------------------------------------------------- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(const uint8_t *src, size_t srclen, char *out, size_t outsz) {
    size_t wi = 0;
    for (size_t i = 0; i < srclen && wi + 4 < outsz; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < srclen) v |= (uint32_t)src[i+1] << 8;
        if (i + 2 < srclen) v |= (uint32_t)src[i+2];
        out[wi++] = b64_table[(v >> 18) & 0x3F];
        out[wi++] = b64_table[(v >> 12) & 0x3F];
        out[wi++] = (i + 1 < srclen) ? b64_table[(v >> 6) & 0x3F] : '=';
        out[wi++] = (i + 2 < srclen) ? b64_table[(v     ) & 0x3F] : '=';
    }
    if (wi < outsz) out[wi] = 0;
}

/* -------------------------------------------------------------------------
 * Socket close — graceful half-close + drain
 * ---------------------------------------------------------------------- */

static void socket_close_graceful(int fd) {
    shutdown(fd, SHUT_WR);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char drain[32];
    while (recv(fd, drain, sizeof(drain), 0) > 0) {}
    close(fd);
}

/* -------------------------------------------------------------------------
 * HTTP transport
 * ---------------------------------------------------------------------- */

static int sock_read_until(int fd, char *buf, int buflen, const char *needle) {
    int total = 0;
    int nlen = (int)strlen(needle);
    while (total < buflen - 1) {
        int r = recv(fd, buf + total, buflen - 1 - total, 0);
        if (r <= 0) break;
        total += r;
        buf[total] = 0;
        if (total >= nlen && strstr(buf, needle)) break;
        vTaskDelay(1);
    }
    return total;
}

static void http_send(int fd, const char *status, const char *ctype, const char *body) {
    char hdr[256];
    int blen = body ? (int)strlen(body) : 0;
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nConnection: close\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",
        status, ctype, blen);
    send(fd, hdr, hlen, 0);
    if (body && blen) send(fd, body, blen, 0);
}

static void http_send_auth_required(int fd) {
    const char *hdr =
        "HTTP/1.1 401 Unauthorized\r\n"
        "Connection: close\r\n"
        "WWW-Authenticate: Basic realm=\"ESP32 UPS Portal\"\r\n"
        "Content-Length: 0\r\n\r\n";
    send(fd, hdr, strlen(hdr), 0);
}

static void http_send_404(int fd) { http_send(fd, "404 Not Found",   "text/plain", "404 Not Found"); }
static void http_send_400(int fd, const char *m) { http_send(fd, "400 Bad Request", "text/plain", m ? m : "400"); }
static void http_send_html(int fd, const char *h) { http_send(fd, "200 OK", "text/html; charset=utf-8", h ? h : ""); }
static void http_send_json(int fd, const char *j) { http_send(fd, "200 OK", "application/json", j ? j : "{}"); }

/* -------------------------------------------------------------------------
 * Basic Auth check
 * ---------------------------------------------------------------------- */

static bool check_auth(const app_cfg_t *cfg, const char *headers, const char *hdr_end) {
    if (!cfg->portal_pass[0]) return true;

    char creds[70];
    snprintf(creds, sizeof(creds), "admin:%s", cfg->portal_pass);
    char expected[100];
    b64_encode((const uint8_t *)creds, strlen(creds), expected, sizeof(expected));

    for (const char *h = headers; h < hdr_end; ) {
        const char *eol = strstr(h, "\r\n");
        if (!eol) break;
        if (strncasecmp(h, "Authorization:", 14) == 0) {
            const char *val = h + 14;
            while (*val == ' ') val++;
            if (strncasecmp(val, "Basic ", 6) == 0) {
                val += 6;
                while (*val == ' ') val++;
                size_t vlen = (size_t)(eol - val);
                size_t elen = strlen(expected);
                if (vlen == elen && memcmp(val, expected, elen) == 0) return true;
            }
            return false;
        }
        h = eol + 2;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Dashboard page   GET /
 *
 * Static table on first load. AJAX polls /status every 5s and updates
 * all live metric cells in-place — no page reload.
 *
 * Cell IDs updated from /status JSON:
 *   td_status   <- ups_status
 *   td_charge   <- battery_charge (%)
 *   td_runtime  <- battery_runtime_s (seconds) or "-"
 *   td_bvolt    <- battery_voltage (V) or "-"
 *   td_ivolt    <- input_voltage (V) or "-"
 *   td_ovolt    <- output_voltage (V) or "-"
 *   td_load     <- ups_load (%) or "-"
 *   td_ip       <- sta_ip
 *
 * Manufacturer / model are static (set at USB enumeration, never change
 * during a session) and are NOT polled.
 * ---------------------------------------------------------------------- */

/*
 * render_dashboard — dynamic table.
 *
 * Static rows (always present):
 *   Manufacturer | Model | Driver | Status | STA IP
 *
 * Optional rows — rendered on initial load only when the field is valid.
 * AJAX polls /status every 5s and dynamically adds rows that become valid
 * or updates rows that already exist.  Rows are never removed once shown.
 *
 * Optional row IDs:
 *   tr_charge   battery.charge (%)
 *   tr_runtime  battery.runtime (s)
 *   tr_bvolt    battery.voltage (V)
 *   tr_load     ups.load (%)
 */
static void render_dashboard(app_cfg_t *cfg, char *out, size_t outsz) {
    ups_state_t ups;
    ups_state_snapshot(&ups);

    const char *st = ups.ups_status[0] ? ups.ups_status : "UNKNOWN";
    char s_mfr[80], s_model[80], sta_ip[16];
    strlcpy0(s_mfr,   ups.manufacturer[0] ? ups.manufacturer : "-", sizeof(s_mfr));
    strlcpy0(s_model, ups.product[0]      ? ups.product      : "-", sizeof(s_model));
    wifi_mgr_sta_ip_str(sta_ip);

    /* Build optional rows for initial static render */
    char opt_rows[512] = {0};
    size_t opt_len = 0;

    if (ups.battery_charge > 0 || ups.valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_charge'><th>Charge</th><td id='td_charge'>%u%%</td></tr>",
            ups.battery_charge);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.battery_runtime_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_runtime'><th>Runtime</th><td id='td_runtime'>%lu s</td></tr>",
            (unsigned long)ups.battery_runtime_s);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.battery_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_bvolt'><th>Batt Voltage</th><td id='td_bvolt'>%.3f V</td></tr>",
            ups.battery_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.ups_load_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_load'><th>Load</th><td id='td_load'>%u%%</td></tr>",
            ups.ups_load_pct);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.input_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_ivolt'><th>Input Voltage</th><td id='td_tr_ivolt'>%.1f V</td></tr>",
            ups.input_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.output_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='tr_ovolt'><th>Output Voltage</th><td id='td_tr_ovolt'>%.1f V</td></tr>",
            ups.output_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    opt_len = strlen(opt_rows);
    (void)opt_len;

    const char *pw_warn = cfg_store_is_default_pass(cfg)
        ? "<p><b>** Security: Default password in use. "
          "<a href='/config'>Change it in Config.</a> **</b></p>"
        : "";

    snprintf(out, outsz,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 UPS Node</title>"
        "</head><body>"
        "<h2>ESP32-S3 UPS Node v15.8</h2>"
        "%s"
        "<table id='ups_tbl' border='1' cellpadding='4' cellspacing='0'>"
        "<tr><th>Manufacturer</th><td>%s</td></tr>"
        "<tr><th>Model</th><td>%s</td></tr>"
        "<tr><th>Driver</th><td>esp32-nut-hid v15.8</td></tr>"
        "<tr><th>Status</th><td id='td_status'><b>%s</b></td></tr>"
        "%s"
        "<tr><th>STA IP</th><td id='td_ip'>%s</td></tr>"
        "</table>"
        "<br><small id='td_poll'>Polling every 5s</small>"
        "<br><br><a href='/config'>[Configure]</a>"
        " &nbsp; <a href='/status'>[Status JSON]</a>"
        " &nbsp; <a href='/compat'>[Compatible UPS List]</a>"
        "<script>"
        /* addOrUpdate(id, label, value) — insert row before STA IP or update existing */
        "function addOrUpdate(id,lbl,val){"
          "var tr=document.getElementById(id);"
          "if(!tr){"
            "tr=document.createElement('tr');"
            "tr.id=id;"
            "tr.innerHTML='<th>'+lbl+'</th><td id=\'td_'+id+'\'></td>';"
            "var ip=document.querySelector('#ups_tbl tr:last-child');"
            "ip.parentNode.insertBefore(tr,ip);"
          "}"
          "var td=document.getElementById('td_'+id);"
          "if(td)td.textContent=val;"
        "}"
        "var t=setInterval(function(){"
          "var x=new XMLHttpRequest();"
          "x.open('GET','/status',true);"
          "x.onload=function(){"
            "if(x.status===200){"
              "try{"
                "var d=JSON.parse(x.responseText);"
                "document.getElementById('td_status').innerHTML='<b>'+d.ups_status+'</b>';"
                "document.getElementById('td_ip').textContent=d.sta_ip;"
                "if(d.battery_charge!==null)addOrUpdate('tr_charge','Charge',d.battery_charge+'%%');"
                "if(d.battery_runtime_s!==null)addOrUpdate('tr_runtime','Runtime',d.battery_runtime_s+' s');"
                "if(d.battery_voltage_v!==null)addOrUpdate('tr_bvolt','Batt Voltage',d.battery_voltage_v.toFixed(3)+' V');"
                "if(d.ups_load_pct!==null)addOrUpdate('tr_load','Load',d.ups_load_pct+'%%');"
                "if(d.input_voltage_v!==null)addOrUpdate('tr_ivolt','Input Voltage',d.input_voltage_v.toFixed(1)+' V');"
                "if(d.output_voltage_v!==null)addOrUpdate('tr_ovolt','Output Voltage',d.output_voltage_v.toFixed(1)+' V');"
                "document.getElementById('td_poll').textContent='Updated: '+new Date().toLocaleTimeString();"
              "}catch(e){}"
            "}"
          "};"
          "x.onerror=function(){"
            "document.getElementById('td_poll').textContent='Poll error - retrying...';"
          "};"
          "x.send();"
        "},5000);"
        "window.addEventListener('beforeunload',function(){clearInterval(t);});"
        "</script>"
        "</body></html>",
        pw_warn,
        s_mfr, s_model,
        st,
        opt_rows,
        sta_ip[0] ? sta_ip : "not connected"
    );

    (void)cfg;
}

/* -------------------------------------------------------------------------
 * Compatible UPS list   GET /compat
 *
 * Static page — source of truth is ups_device_db.c.
 * Confirmed devices (tested and working) are marked with a green checkmark.
 * All other entries are "reported compatible" based on NUT source / known
 * HID UPS standards but have not been personally confirmed by the author.
 * ---------------------------------------------------------------------- */
static void render_compat(char *out, size_t outsz) {
    snprintf(out, outsz,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 UPS Node - Compatible UPS List</title>"
        "<style>"
        "body{font-family:sans-serif;margin:16px}"
        "table{border-collapse:collapse;width:100%%}"
        "th,td{border:1px solid #aaa;padding:6px 10px;text-align:left;font-size:0.92em}"
        "th{background:#e8e8e8}"
        ".confirmed{color:#186a00;font-weight:bold}"
        ".unconfirmed{color:#555}"
        ".note{font-size:0.85em;color:#444}"
        "</style>"
        "</head><body>"
        "<h2>ESP32-S3 UPS Node v15.8 - Compatible UPS List</h2>"
        "<p>Devices marked <span class='confirmed'>&#10003; Confirmed</span> have been personally tested "
        "and verified working with this firmware.<br>"
        "All other devices are expected to work based on NUT/HID UPS standards "
        "but have not been independently confirmed by the project author.</p>"
        "<p><b>Have a device that works? Open an issue or PR on "
        "<a href='https://github.com/Driftah9/esp32-s3-nut-node'>GitHub</a> "
        "to get it added to the confirmed list.</b></p>"
        "<table>"
        "<tr><th>Vendor</th><th>Models / Series</th><th>VID:PID</th>"
        "<th>Decode Mode</th><th>Notes</th><th>Status</th></tr>"

        /* ---- APC / Schneider ---- */
        "<tr>"
        "<td>APC / Schneider</td>"
        "<td>Back-UPS XS 1500M</td>"
        "<td>051D:0002</td>"
        "<td>Direct (APC Back-UPS)</td>"
        "<td class='note'>Charge, runtime, status via interrupt IN.<br>"
        "Voltages via GET_REPORT (Feature polling).</td>"
        "<td class='confirmed'>&#10003; Confirmed</td>"
        "</tr>"

        "<tr>"
        "<td>APC / Schneider</td>"
        "<td>Back-UPS BR1000G</td>"
        "<td>051D:0002</td>"
        "<td>Direct (APC Back-UPS)</td>"
        "<td class='note'>Same VID:PID as XS 1500M. Same decode path confirmed working.</td>"
        "<td class='confirmed'>&#10003; Confirmed</td>"
        "</tr>"

        "<tr>"
        "<td>APC / Schneider</td>"
        "<td>Smart-UPS / other models</td>"
        "<td>051D:0000 (wildcard)</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID descriptor path. Vendor page remap applied.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- CyberPower ---- */
        "<tr>"
        "<td>CyberPower</td>"
        "<td>SX550G, CP1200AVR, CP825AVR-G, CP1000AVRLCD, CP1500C,<br>"
        "CP550HG, CP1000PFCLCD, CP850PFCLCD, CP1350PFCLCD,<br>"
        "CP1500PFCLCD, CP1350AVRLCD, CP1500AVRLCD, CP900AVR,<br>"
        "CPS685AVR, CPS800AVR, EC350G, EC750G, EC850LCD,<br>"
        "BL1250U, AE550, CPJ500</td>"
        "<td>0764:0501</td>"
        "<td>Direct (CyberPower)</td>"
        "<td class='note'>All live values via direct-decode bypass (rids 0x20-0x88).<br>"
        "Descriptor declares insufficient Input fields; direct decode required.</td>"
        "<td class='confirmed'>&#10003; Confirmed</td>"
        "</tr>"

        "<tr>"
        "<td>CyberPower</td>"
        "<td>OR2200LCDRM2U, OR700LCDRM1U, OR500LCDRM1U, OR1500ERM1U,<br>"
        "CP1350EPFCLCD, CP1500EPFCLCD, PR1500RT2U, PR6000LCDRTXL5U,<br>"
        "RT650EI, UT2200E, Value 1500ELCD-RU, VP1200ELCD</td>"
        "<td>0764:0601</td>"
        "<td>Direct (CyberPower)</td>"
        "<td class='note'>Same direct-decode path as PID 0x0501. "
        "Active power LogMax fix also applied.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        "<tr>"
        "<td>CyberPower</td>"
        "<td>900AVR, BC900D</td>"
        "<td>0764:0005</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Older model. Standard path with voltage LogMax fix.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        "<tr>"
        "<td>CyberPower (other)</td>"
        "<td>Any CyberPower not listed above</td>"
        "<td>0764:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>VID-only wildcard fallback. Standard path, voltage quirks applied.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Eaton / MGE ---- */
        "<tr>"
        "<td>Eaton / MGE / Powerware</td>"
        "<td>3S, 5E, 5P, Ellipse, Evolution</td>"
        "<td>0463:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Tripp Lite ---- */
        "<tr>"
        "<td>Tripp Lite</td>"
        "<td>OMNI, SMART, INTERNETOFFICE series</td>"
        "<td>09AE:xxxx</td>"
        "<td>Standard HID + GET_REPORT</td>"
        "<td class='note'>Standard HID path. Feature report polling active for values "
        "only available via GET_REPORT.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Belkin ---- */
        "<tr>"
        "<td>Belkin</td>"
        "<td>F6H, F6C series</td>"
        "<td>050D:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Liebert ---- */
        "<tr>"
        "<td>Liebert / Vertiv</td>"
        "<td>GXT4, PSI5</td>"
        "<td>10AF:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Powercom ---- */
        "<tr>"
        "<td>Powercom</td>"
        "<td>Black Knight, Dragon, King Pro</td>"
        "<td>0D9F:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- HP ---- */
        "<tr>"
        "<td>HP</td>"
        "<td>T750 G2/G3, T1000 G2/G3, T1500 G2/G3, T3000 G2/G3</td>"
        "<td>03F0:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        /* ---- Dell ---- */
        "<tr>"
        "<td>Dell</td>"
        "<td>H750E, H950E, H1000E, H1750E</td>"
        "<td>047C:xxxx</td>"
        "<td>Standard HID</td>"
        "<td class='note'>Standard HID UPS descriptor. No known quirks.</td>"
        "<td class='unconfirmed'>&#9711; Unconfirmed</td>"
        "</tr>"

        "</table>"
        "<br><p class='note'>VID:PID format: xxxx = any product ID (VID-only wildcard match).<br>"
        "Decode modes: <b>Standard HID</b> = generic USB HID Power Device descriptor path. "
        "<b>Direct</b> = vendor-specific byte-position decode (used when descriptor is incomplete).<br>"
        "GET_REPORT = Feature report polling via USB control transfer for values not on interrupt IN.</p>"
        "<br><a href='/'>[Back to Status]</a>"
        "</body></html>"
    );
}

/* -------------------------------------------------------------------------
 * Config page   GET /config
 * ---------------------------------------------------------------------- */

static void render_config(app_cfg_t *cfg, char *out, size_t outsz,
                          const char *note, const char *note_cls) {
    char sta_ip[16];
    wifi_mgr_sta_ip_str(sta_ip);

    bool ap_up = wifi_mgr_ap_is_active();

    char note_html[200] = {0};
    if (note && note[0])
        snprintf(note_html, sizeof(note_html), "<p><b>%s</b></p>", note);

    const char *pw_warn = cfg_store_is_default_pass(cfg)
        ? "<p><b>** Default password \"upsmon\" is in use. "
          "Change it below under Portal Security. **</b></p>"
        : "";

    snprintf(out, outsz,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 UPS Config</title>"
        "</head><body>"
        "<h2>ESP32-S3 UPS Node v15.8 - Config</h2>"
        "%s%s"
        "<form method='POST' action='/save'>"
        "<table border='0' cellpadding='4'>"
        "<tr><th colspan='2' align='left'>Wi-Fi (STA)</th></tr>"
        "<tr><td>SSID:</td>"
            "<td><input name='sta_ssid' size='32' maxlength='32' value='%s'></td></tr>"
        "<tr><td>Password:</td>"
            "<td><input name='sta_pass' type='password' size='32' maxlength='64' value='%s'></td></tr>"
        "<tr><th colspan='2' align='left'>Soft AP (%s)</th></tr>"
        "<tr><td>AP SSID:</td>"
            "<td><input name='ap_ssid' size='32' maxlength='32' value='%s'></td></tr>"
        "<tr><td>AP Password (8+ chars):</td>"
            "<td><input name='ap_pass' type='password' size='32' maxlength='64' value='%s'></td></tr>"
        "<tr><th colspan='2' align='left'>NUT Identity</th></tr>"
        "<tr><td>UPS Name:</td>"
            "<td><input name='ups_name' size='24' maxlength='32' value='%s'></td></tr>"
        "<tr><td>NUT Username:</td>"
            "<td><input name='nut_user' size='24' maxlength='32' value='%s'></td></tr>"
        "<tr><td>NUT Password:</td>"
            "<td><input name='nut_pass' type='password' size='24' maxlength='32' value='%s'></td></tr>"
        "<tr><th colspan='2' align='left'>Portal Security (login: admin / &lt;password&gt;)</th></tr>"
        "<tr><td>New Password:</td>"
            "<td><input name='portal_pass' type='password' size='24' maxlength='32' "
            "placeholder='blank = keep current'></td></tr>"
        "</table>"
        "<br><input type='submit' value='Save and Apply'>"
        "</form>"
        "<br><a href='/'>[Back to Status]</a>"
        " &nbsp; <a href='/reboot' onclick=\"return confirm('Reboot?')\">[Reboot]</a>"
        " &nbsp; <small>STA: %s</small>"
        "</body></html>",
        pw_warn, note_html,
        cfg->sta_ssid, cfg->sta_pass,
        ap_up ? "Active" : "Off",
        cfg->ap_ssid, cfg->ap_pass,
        cfg->ups_name, cfg->nut_user, cfg->nut_pass,
        sta_ip[0] ? sta_ip : "not connected"
    );

    (void)note_cls;
}

/* -------------------------------------------------------------------------
 * Form parser
 * ---------------------------------------------------------------------- */

static void parse_form_kv(app_cfg_t *cfg_inout, const char *body,
                           char *action_out, size_t action_sz) {
    if (action_out && action_sz) action_out[0] = 0;

    char tmp[HTTP_BODY_MAX + 1];
    strlcpy0(tmp, body ? body : "", sizeof(tmp));

    char *saveptr = NULL;
    for (char *tok = strtok_r(tmp, "&", &saveptr); tok; tok = strtok_r(NULL, "&", &saveptr)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = tok, *v = eq + 1;
        url_decode_inplace(k);
        url_decode_inplace(v);
        trim_ws(k);
        if      (!strcmp(k, "sta_ssid"))   strlcpy0(cfg_inout->sta_ssid,   v, sizeof(cfg_inout->sta_ssid));
        else if (!strcmp(k, "sta_pass"))   strlcpy0(cfg_inout->sta_pass,   v, sizeof(cfg_inout->sta_pass));
        else if (!strcmp(k, "ap_ssid"))    strlcpy0(cfg_inout->ap_ssid,    v, sizeof(cfg_inout->ap_ssid));
        else if (!strcmp(k, "ap_pass"))    strlcpy0(cfg_inout->ap_pass,    v, sizeof(cfg_inout->ap_pass));
        else if (!strcmp(k, "ups_name"))   strlcpy0(cfg_inout->ups_name,   v, sizeof(cfg_inout->ups_name));
        else if (!strcmp(k, "nut_user"))   strlcpy0(cfg_inout->nut_user,   v, sizeof(cfg_inout->nut_user));
        else if (!strcmp(k, "nut_pass"))   strlcpy0(cfg_inout->nut_pass,   v, sizeof(cfg_inout->nut_pass));
        else if (!strcmp(k, "portal_pass") && v[0])
            strlcpy0(cfg_inout->portal_pass, v, sizeof(cfg_inout->portal_pass));
        else if (!strcmp(k, "action") && action_out && action_sz)
            strlcpy0(action_out, v, action_sz);
    }
}

/* -------------------------------------------------------------------------
 * HTTP client handler
 * ---------------------------------------------------------------------- */

static void handle_http_client(app_cfg_t *cfg, int fd) {
    char *rx = (char *)malloc(HTTP_RX_MAX + 1);
    if (!rx) { ESP_LOGE(TAG, "rx malloc failed"); close(fd); return; }

    int n = sock_read_until(fd, rx, HTTP_RX_MAX + 1, "\r\n\r\n");
    if (n <= 0) { free(rx); socket_close_graceful(fd); return; }

    char *line_end = strstr(rx, "\r\n");
    if (!line_end) { http_send_400(fd, "Bad request"); free(rx); socket_close_graceful(fd); return; }
    *line_end = 0;

    char method[8] = {0}, uri[256] = {0};
    if (sscanf(rx, "%7s %255s", method, uri) != 2) {
        http_send_400(fd, "Bad request line"); free(rx); socket_close_graceful(fd); return;
    }

    char path[256];
    strlcpy0(path, uri, sizeof(path));
    char *qmark = strchr(path, '?');
    const char *query = "";
    if (qmark) { *qmark = 0; query = qmark + 1; }

    char *headers = line_end + 2;
    char *hdr_end = strstr(headers, "\r\n\r\n");
    if (!hdr_end) { http_send_400(fd, "Bad headers"); free(rx); socket_close_graceful(fd); return; }
    char *body = hdr_end + 4;

    int content_len = 0;
    for (char *h = headers; h < hdr_end; ) {
        char *eol = strstr(h, "\r\n");
        if (!eol) break;
        *eol = 0;
        if (strncasecmp(h, "Content-Length:", 15) == 0) content_len = atoi(h + 15);
        *eol = '\r';
        h = eol + 2;
    }

    int already = (int)strlen(body);
    if (strcasecmp(method, "POST") == 0) {
        if (content_len > HTTP_BODY_MAX) {
            http_send_400(fd, "Body too large"); free(rx); socket_close_graceful(fd); return;
        }
        if (content_len > already) {
            int rem = content_len - already;
            while (rem > 0) {
                int r = recv(fd, rx + n, HTTP_RX_MAX - n, 0);
                if (r <= 0) break;
                n += r; rx[n] = 0; rem -= r;
            }
            hdr_end = strstr(rx, "\r\n\r\n");
            if (!hdr_end) { http_send_400(fd, "Bad request"); free(rx); socket_close_graceful(fd); return; }
            body = hdr_end + 4;
        }
        if (content_len >= 0 && (int)strlen(body) > content_len) body[content_len] = 0;
    }

    /* Auth: /status and /compat always open (public endpoints) */
    bool needs_auth = (strcmp(path, "/status") != 0) && (strcmp(path, "/compat") != 0);
    if (needs_auth && !check_auth(cfg, headers, hdr_end)) {
        http_send_auth_required(fd);
        free(rx); socket_close_graceful(fd); return;
    }

    /* ---- Route dispatch ---- */

    if (strcmp(path, "/") == 0 && strcasecmp(method, "GET") == 0) {
        char *page = (char *)malloc(HTTP_PAGE_BUF);
        if (page) { render_dashboard(cfg, page, HTTP_PAGE_BUF); http_send_html(fd, page); free(page); }
        else       { http_send(fd, "500 Internal Server Error", "text/plain", "OOM"); }

    } else if (strcmp(path, "/compat") == 0 && strcasecmp(method, "GET") == 0) {
        /* Compatible UPS list — needs a larger buffer due to table size */
        char *page = (char *)malloc(8192);
        if (page) { render_compat(page, 8192); http_send_html(fd, page); free(page); }
        else       { http_send(fd, "500 Internal Server Error", "text/plain", "OOM"); }

    } else if ((strcmp(path, "/config") == 0 || strcmp(path, "/config/") == 0)
               && strcasecmp(method, "GET") == 0) {
        char *page = (char *)malloc(HTTP_PAGE_BUF);
        if (page) { render_config(cfg, page, HTTP_PAGE_BUF, NULL, NULL); http_send_html(fd, page); free(page); }
        else       { http_send(fd, "500 Internal Server Error", "text/plain", "OOM"); }

    } else if (strcmp(path, "/status") == 0 && strcasecmp(method, "GET") == 0) {
        /* Full live JSON — all fields consumed by AJAX poller on dashboard.
         * Numeric fields use JSON null when not valid/available. */
        char sta_ip[16];
        wifi_mgr_sta_ip_str(sta_ip);
        ups_state_t ups;
        ups_state_snapshot(&ups);

        char json[640];
        char bvolt_s[20], load_s[12], runtime_s[12], ivolt_s[20], ovolt_s[20];

        /* Format nullable floats/ints as JSON null or value */
        if (ups.battery_voltage_valid)
            snprintf(bvolt_s,   sizeof(bvolt_s),   "%.3f", ups.battery_voltage_mv / 1000.0f);
        else strlcpy0(bvolt_s, "null", sizeof(bvolt_s));

        if (ups.ups_load_valid)
            snprintf(load_s,    sizeof(load_s),     "%u",   ups.ups_load_pct);
        else strlcpy0(load_s,  "null", sizeof(load_s));

        if (ups.battery_runtime_valid)
            snprintf(runtime_s, sizeof(runtime_s),  "%lu",  (unsigned long)ups.battery_runtime_s);
        else strlcpy0(runtime_s, "null", sizeof(runtime_s));

        if (ups.input_voltage_valid)
            snprintf(ivolt_s, sizeof(ivolt_s), "%.3f", ups.input_voltage_mv  / 1000.0f);
        else strlcpy0(ivolt_s, "null", sizeof(ivolt_s));

        if (ups.output_voltage_valid)
            snprintf(ovolt_s, sizeof(ovolt_s), "%.3f", ups.output_voltage_mv / 1000.0f);
        else strlcpy0(ovolt_s, "null", sizeof(ovolt_s));

        snprintf(json, sizeof(json),
            "{"
            "\"ap_ssid\":\"%s\","
            "\"sta_ssid\":\"%s\","
            "\"sta_ip\":\"%s\","
            "\"ups_name\":\"%s\","
            "\"nut_port\":3493,"
            "\"ups_status\":\"%s\","
            "\"battery_charge\":%u,"
            "\"battery_runtime_s\":%s,"
            "\"battery_voltage_v\":%s,"
            "\"ups_load_pct\":%s,"
            "\"input_voltage_v\":%s,"
            "\"output_voltage_v\":%s,"
            "\"ups_valid\":%s,"
            "\"ap_active\":%s"
            "}",
            cfg->ap_ssid,
            cfg->sta_ssid,
            sta_ip,
            cfg->ups_name,
            ups.ups_status[0] ? ups.ups_status : "UNKNOWN",
            ups.battery_charge,
            runtime_s, bvolt_s, load_s,
            ivolt_s, ovolt_s,
            ups.valid               ? "true" : "false",
            wifi_mgr_ap_is_active() ? "true" : "false");

        http_send_json(fd, json);

    } else if (strcmp(path, "/save") == 0
               && (strcasecmp(method, "POST") == 0 || strcasecmp(method, "GET") == 0)) {

        const char *payload = (strcasecmp(method, "POST") == 0) ? body : query;
        app_cfg_t newcfg = *cfg;
        char action[32];
        parse_form_kv(&newcfg, payload, action, sizeof(action));

        char *page = (char *)malloc(HTTP_PAGE_BUF);
        if (!page) {
            http_send(fd, "500 Internal Server Error", "text/plain", "OOM");
            free(rx); socket_close_graceful(fd); return;
        }

        if (!is_printable_ascii(newcfg.ap_ssid) || strlen(newcfg.ap_ssid) == 0) {
            render_config(cfg, page, HTTP_PAGE_BUF, "ERROR: Invalid AP SSID.", "err");
            http_send_html(fd, page);
            free(page); free(rx); socket_close_graceful(fd); return;
        }

        *cfg = newcfg;
        esp_err_t err = cfg_store_commit(cfg);
        const char *note = (err == ESP_OK) ? "Configuration saved." : "Save failed.";
        render_config(cfg, page, HTTP_PAGE_BUF, note, (err == ESP_OK) ? "ok" : "err");
        http_send_html(fd, page);
        free(page);

    } else if (strcmp(path, "/reboot") == 0 && strcasecmp(method, "GET") == 0) {
        http_send(fd, "200 OK", "text/plain", "Rebooting...\n");
        free(rx); socket_close_graceful(fd);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;

    } else {
        http_send_404(fd);
    }

    free(rx);
    socket_close_graceful(fd);
}

/* -------------------------------------------------------------------------
 * HTTP server task
 * ---------------------------------------------------------------------- */

static void http_server_task(void *arg) {
    app_cfg_t *cfg = (app_cfg_t*)arg;

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) { ESP_LOGE(TAG, "socket() failed"); vTaskDelete(NULL); return; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed"); close(s); vTaskDelete(NULL); return;
    }
    if (listen(s, 4) != 0) {
        ESP_LOGE(TAG, "listen() failed"); close(s); vTaskDelete(NULL); return;
    }

    ESP_LOGI(TAG, "HTTP portal on :%d  / = dashboard  /config = settings", HTTP_PORT);

    while (1) {
        struct sockaddr_in6 src;
        socklen_t slen = sizeof(src);
        int c = accept(s, (struct sockaddr *)&src, &slen);
        if (c < 0) continue;
        handle_http_client(cfg, c);
    }
}

void http_portal_start(app_cfg_t *cfg) {
    xTaskCreate(http_server_task, "http_srv", 6144, cfg, 5, NULL);
}
