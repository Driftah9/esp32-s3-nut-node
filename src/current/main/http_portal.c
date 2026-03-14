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
 R16  v15.9  Full UI redesign — dark industrial theme. Inline CSS via PORTAL_CSS
             macro shared across all pages. HTTP_PAGE_BUF 8192, /compat 10240.
             Status color-coded by JS (OL=green, OB=amber, fault=red).
             Monospace data values, sans-serif labels. Responsive viewport.
 R17  v15.10 Version bump. CyberPower OB fix (rid=0x29 authoritative, rid=0x80
             ignored when ac=1). LB false-positive removed (bit1 != low_batt).
             http_compat.c split from http_portal.c. Live poll clock on dashboard.

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
#define HTTP_PAGE_BUF  8192   /* v15.9: dark theme CSS inline */
#define HTTP_COMPAT_BUF 49152 /* v15.9: full expandable compat table — all vendors */

/* Shared CSS injected into every page — dark industrial theme, no external resources */
#define PORTAL_CSS \
    "<style>" \
    "*{box-sizing:border-box;margin:0;padding:0}" \
    "body{background:#111;color:#e8e8e2;font-family:'Courier New',Courier,monospace;" \
         "font-size:14px;padding:20px 16px;max-width:700px}" \
    "h2{font-family:Arial,Helvetica,sans-serif;font-weight:600;" \
       "letter-spacing:0.04em;color:#e8e8e2;margin-bottom:4px;font-size:1.1em}" \
    ".subtitle{color:#666;font-size:0.8em;margin-bottom:20px;font-family:Arial,sans-serif}" \
    ".warn{background:#2a1800;border-left:3px solid #ffab00;color:#ffcc66;" \
          "padding:8px 12px;margin-bottom:16px;font-size:0.85em;font-family:Arial,sans-serif}" \
    ".warn a{color:#ffcc66}" \
    "table{border-collapse:collapse;width:100%%;margin-bottom:16px}" \
    "th,td{padding:7px 10px;text-align:left;border:1px solid #2a2a2a;vertical-align:top}" \
    "th{background:#1c1c1c;color:#888;font-weight:normal;font-family:Arial,sans-serif;" \
       "font-size:0.82em;text-transform:uppercase;letter-spacing:0.06em;min-width:130px}" \
    "td{color:#e8e8e2;font-family:'Courier New',Courier,monospace}" \
    "tr:hover td,tr:hover th{background:#161616}" \
    ".status-ol{color:#00c853;font-weight:bold}" \
    ".status-ob{color:#ffab00;font-weight:bold}" \
    ".status-fault{color:#ff3d00;font-weight:bold}" \
    ".status-unknown{color:#888}" \
    ".nav{margin-top:16px;font-family:Arial,sans-serif;font-size:0.82em}" \
    ".nav a{color:#4fc3f7;text-decoration:none;margin-right:16px}" \
    ".nav a:hover{color:#81d4fa}" \
    ".poll{color:#aaa;font-size:0.75em;font-family:Arial,sans-serif;margin-top:8px}" \
    "input[type=text],input[type=password],input:not([type]){" \
       "background:#1c1c1c;border:1px solid #333;color:#e8e8e2;" \
       "padding:5px 8px;font-family:'Courier New',Courier,monospace;font-size:13px;width:260px}" \
    "input:focus{outline:none;border-color:#4fc3f7}" \
    ".form-section{color:#4fc3f7;font-family:Arial,sans-serif;font-size:0.8em;" \
                 "text-transform:uppercase;letter-spacing:0.08em;" \
                 "padding:10px 0 4px;border-top:1px solid #2a2a2a;margin-top:8px}" \
    ".form-row{display:flex;align-items:center;padding:5px 0;border-bottom:1px solid #1c1c1c}" \
    ".form-label{color:#888;font-family:Arial,sans-serif;font-size:0.82em;" \
               "text-transform:uppercase;letter-spacing:0.05em;width:200px;flex-shrink:0}" \
    ".btn{background:#1c1c1c;border:1px solid #333;color:#e8e8e2;" \
         "padding:7px 18px;cursor:pointer;font-family:Arial,sans-serif;" \
         "font-size:0.85em;margin-top:14px;letter-spacing:0.04em}" \
    ".btn:hover{border-color:#4fc3f7;color:#4fc3f7}" \
    ".note-ok{color:#00c853;font-family:Arial,sans-serif;font-size:0.85em;padding:8px 0;margin-bottom:8px}" \
    ".note-err{color:#ff3d00;font-family:Arial,sans-serif;font-size:0.85em;padding:8px 0;margin-bottom:8px}" \
    "</style>"

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
            "<tr id='tr_runtime'><th>Runtime</th><td id='td_runtime'>%lum %02lus</td></tr>",
            (unsigned long)ups.battery_runtime_s / 60,
            (unsigned long)ups.battery_runtime_s % 60);
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

    /* Determine initial status CSS class */
    const char *st_cls = "status-unknown";
    if (strstr(st, "OB") || strstr(st, "CHRG")) st_cls = "status-ob";
    else if (strstr(st, "OL"))                   st_cls = "status-ol";
    else if (strstr(st, "LB") || strstr(st, "RB") || strstr(st, "ALARM")) st_cls = "status-fault";

    const char *pw_warn = cfg_store_is_default_pass(cfg)
        ? "<div class='warn'>Default password in use. "
          "<a href='/config'>Change it in Config.</a></div>"
        : "";

    snprintf(out, outsz,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>UPS Node</title>"
        PORTAL_CSS
        "</head><body>"
        "<h2>ESP32-S3 UPS Node</h2>"
        "<div class='subtitle'>v15.10 &mdash; NUT server on tcp/3493</div>"
        "%s"
        "<table id='ups_tbl'>"
        "<tr><th>Manufacturer</th><td>%s</td></tr>"
        "<tr><th>Model</th><td>%s</td></tr>"
        "<tr><th>Driver</th><td>esp32-nut-hid v15.10</td></tr>"
        "<tr><th>Status</th><td id='td_status' class='%s'>%s</td></tr>"
        "%s"
        "<tr><th>STA IP</th><td id='td_ip'>%s</td></tr>"
        "</table>"
        "<div class='poll' id='td_poll'></div>"
        "<div class='nav'>"
        "<a href='/config'>Configure</a>"
        "<a href='/status'>Status JSON</a>"
        "<a href='/compat'>Compatible UPS List</a>"
        "</div>"
        "<script>"
        "function stCls(s){"
          "if(s.indexOf('OB')>=0||s.indexOf('CHRG')>=0)return 'status-ob';"
          "if(s.indexOf('OL')>=0)return 'status-ol';"
          "if(s.indexOf('LB')>=0||s.indexOf('RB')>=0||s.indexOf('ALARM')>=0)return 'status-fault';"
          "return 'status-unknown';"
        "}"
        "function addOrUpdate(id,lbl,val){"
          "var tr=document.getElementById(id);"
          "if(!tr){"
            "tr=document.createElement('tr');"
            "tr.id=id;"
            "var ht=document.createElement('th');ht.textContent=lbl;var dt=document.createElement('td');dt.id='td_'+id;tr.appendChild(ht);tr.appendChild(dt);"
            "var ip=document.querySelector('#ups_tbl tr:last-child');"
            "ip.parentNode.insertBefore(tr,ip);"
          "}"
          "var td=document.getElementById('td_'+id);"
          "if(td)td.textContent=val;"
        "}"
"var lastOk=Date.now();"
"function fmtAge(){"
  "var s=Math.round((Date.now()-lastOk)/1000);"
  "if(s<5)return'Just updated';"
  "if(s<60)return s+'s ago';"
  "return Math.floor(s/60)+'m '+('0'+s%%60).slice(-2)+'s ago';"
"}"
"var ck=setInterval(function(){"
  "document.getElementById('td_poll').textContent=fmtAge();"
"},1000);"
"function doPoll(){"
  "var x=new XMLHttpRequest();"
  "x.open('GET','/status',true);"
  "x.onload=function(){"
    "if(x.status===200){"
      "try{"
        "var d=JSON.parse(x.responseText);"
        "var sc=document.getElementById('td_status');"
        "sc.className=stCls(d.ups_status);sc.textContent=d.ups_status;"
        "document.getElementById('td_ip').textContent=d.sta_ip;"
        "if(d.battery_charge!==null)addOrUpdate('tr_charge','Charge',d.battery_charge+'%%');"
        "if(d.battery_runtime_s!==null){var rs=d.battery_runtime_s;"
        "var rm=Math.floor(rs/60);var rse=rs%%60;"
        "var rt=rm>0?(rm+'m '+(rse<10?'0':'')+rse+'s'):rse+'s';"
        "addOrUpdate('tr_runtime','Runtime',rt);}"
        "if(d.battery_voltage_v!==null)addOrUpdate('tr_bvolt','Batt Voltage',d.battery_voltage_v.toFixed(3)+' V');"
        "if(d.input_voltage_v!==null)addOrUpdate('tr_ivolt','Input Voltage',d.input_voltage_v.toFixed(1)+' V');"
        "if(d.output_voltage_v!==null)addOrUpdate('tr_ovolt','Output Voltage',d.output_voltage_v.toFixed(1)+' V');"
        "if(d.ups_load_pct!==null)addOrUpdate('tr_load','Load',d.ups_load_pct+'%%');"
        "lastOk=Date.now();"
      "}catch(e){}"
    "}"
  "};"
  "x.onerror=function(){"
    "document.getElementById('td_poll').textContent='Poll error \u2014 retrying...';"
  "};"
  "x.send();"
"}"
"doPoll();"
"var t=setInterval(doPoll,5000);"
"window.addEventListener('beforeunload',function(){clearInterval(t);clearInterval(ck);});"
        





        "</script>"
        "</body></html>",
        pw_warn,
        s_mfr, s_model,
        st_cls, st,
        opt_rows,
        sta_ip[0] ? sta_ip : "not connected"
    );

    (void)cfg;
}

/* -------------------------------------------------------------------------
 * Compatible UPS list   GET /compat
 *
 * Two-level expandable hierarchy: Vendor -> Series -> Model table.
 * Built with strlcat appends to avoid single-snprintf stack overflow.
 * All data sourced from NUT usbhid-ups driver hardware compatibility list.
 * ---------------------------------------------------------------------- */

/* Append a CSS+JS compat page header into buf */
static void compat_head(char *buf, size_t sz) {
    strlcat(buf,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Compatible UPS List</title>"
        PORTAL_CSS
        "<style>"
        "body{max-width:none;padding:20px}"
        ".ok{color:#00c853;font-size:0.75em;font-family:Arial,sans-serif;white-space:nowrap}"
        ".ex{color:#7986cb;font-size:0.75em;font-family:Arial,sans-serif;white-space:nowrap}"
        ".un{color:#444;font-size:0.75em;font-family:Arial,sans-serif;white-space:nowrap}"
        ".vbtn{width:100%%;background:#1c1c1c;border:none;border-top:1px solid #2a2a2a;"
              "color:#e8e8e2;font-family:Arial,sans-serif;font-size:0.85em;"
              "padding:9px 14px;text-align:left;cursor:pointer;"
              "display:flex;align-items:center;gap:10px}"
        ".vbtn:hover{background:#242424}"
        ".vbtn.first{border-top:1px solid #333}"
        ".arr{color:#555;font-size:0.68em;transition:transform 0.14s;"
             "display:inline-block;width:12px;flex-shrink:0}"
        ".vbtn.open .arr{transform:rotate(90deg);color:#4fc3f7}"
        ".vn{font-weight:600;min-width:200px}"
        ".vp{color:#777;font-family:'Courier New',Courier,monospace;font-size:0.86em;min-width:120px}"
        ".vc{margin-left:auto;font-size:0.74em;color:#555;white-space:nowrap}"
        ".vb{font-size:0.74em;margin-left:10px;white-space:nowrap}"
        ".vpanel{display:none;border-bottom:1px solid #2a2a2a}"
        ".vpanel.open{display:block}"
        ".sr{border-top:1px solid #1d1d1d}"
        ".sbtn{width:100%%;background:#141414;border:none;color:#e8e8e2;"
              "font-family:Arial,sans-serif;font-size:0.79em;"
              "padding:7px 14px 7px 28px;text-align:left;cursor:pointer;"
              "display:flex;align-items:center;gap:8px}"
        ".sbtn:hover{background:#181818}"
        ".sarr{color:#444;font-size:0.65em;transition:transform 0.14s;"
              "display:inline-block;width:10px;flex-shrink:0}"
        ".sbtn.open .sarr{transform:rotate(90deg);color:#777}"
        ".sn{color:#aaa;min-width:220px}"
        ".sp{color:#555;font-family:'Courier New',Courier,monospace;font-size:0.86em;min-width:110px}"
        ".sc{margin-left:auto;font-size:0.73em;color:#444;white-space:nowrap}"
        ".sb{font-size:0.73em;margin-left:10px;white-space:nowrap}"
        ".mt{display:none;width:100%%;min-width:980px;border-collapse:collapse;table-layout:fixed}"
        ".mt.open{display:table}"
        ".mt th{background:#0e0e0e;color:#555;font-family:Arial,sans-serif;"
               "font-size:0.71em;text-transform:uppercase;letter-spacing:0.07em;"
               "padding:5px 10px 5px 40px;font-weight:normal;"
               "border-bottom:1px solid #1a1a1a;text-align:left}"
        ".mt th:nth-child(1){width:280px;padding-left:40px}"
        ".mt th:nth-child(2){width:110px}"
        ".mt th:nth-child(3){width:180px}"
        ".mt th:nth-child(4){width:300px}"
        ".mt th:nth-child(5){width:110px}"
        ".mt td{padding:5px 10px;border-bottom:1px solid #131313;vertical-align:top;"
               "overflow:hidden;text-overflow:ellipsis}"
        ".mt td:first-child{padding-left:40px}"
        ".mt tr:last-child td{border-bottom:none}"
        ".mn{color:#c8c8c2;font-size:0.87em}"
        ".mp{color:#555;font-size:0.82em}"
        ".mm{color:#666;font-family:Arial,sans-serif;font-size:0.75em;line-height:1.4}"
        ".mnt{color:#555;font-family:Arial,sans-serif;font-size:0.75em;line-height:1.4}"
        "</style>"
        "</head><body>"
        "<h2>Compatible UPS List</h2>"
        "<div class='subtitle'>ESP32-S3 UPS Node v15.10 "
        "&mdash; NUT usbhid-ups driver &mdash; 29 manufacturers / 338+ devices</div>"
        "<div style='font-family:Arial,sans-serif;font-size:0.82em;color:#888;"
             "margin-bottom:14px;line-height:1.6'>"
        "Click a vendor to expand series. Click a series to expand models. "
        "<span style='color:#00c853'>&#10003; Confirmed</span> = personally tested. "
        "<span style='color:#7986cb'>&#9711; Expected</span> = same VID:PID, untested. "
        "<span style='color:#444'>&#9711; Unconfirmed</span> = standard HID path, untested.<br>"
        "Have a working device not listed? "
        "<a href='https://github.com/Driftah9/esp32-s3-nut-node' style='color:#4fc3f7'>"
        "Open an issue on GitHub</a> to get it added.</div>"
        "<div style='margin-bottom:10px;font-family:Arial,sans-serif;font-size:0.8em'>"
        "<button onclick='expandAll()' style='background:#1c1c1c;border:1px solid #333;"
            "color:#4fc3f7;padding:5px 14px;cursor:pointer;font-size:0.85em;"
            "font-family:Arial,sans-serif;margin-right:8px'>&#9660; Expand All</button>"
        "<button onclick='collapseAll()' style='background:#1c1c1c;border:1px solid #333;"
            "color:#888;padding:5px 14px;cursor:pointer;font-size:0.85em;"
            "font-family:Arial,sans-serif'>&#9650; Collapse All</button>"
        "</div>",
        sz);
}

/* Macros to reduce repetition in render_compat body */
#define CAT(b,s,x)   strlcat((x),(s),(b))
#define VOPEN(b,x,nm,pid,badge,bclr,cnt) do { \
    char _vt[512]; \
    snprintf(_vt,sizeof(_vt), \
        "<button class='vbtn' onclick='tv(this)'>" \
        "<span class='arr'>&#9654;</span>" \
        "<span class='vn'>%s</span>" \
        "<span class='vp'>%s</span>" \
        "<span class='vb' style='color:%s'>%s</span>" \
        "<span class='vc'>%s</span></button>" \
        "<div class='vpanel'>", \
        (nm),(pid),(bclr),(badge),(cnt)); \
    strlcat((x),_vt,(b)); } while(0)
#define VCLOSE(b,x)  strlcat((x),"</div>",(b))
#define SOPEN(b,x,nm,pid,badge,bclr,cnt) do { \
    char _st[640]; \
    snprintf(_st,sizeof(_st), \
        "<div class='sr'><button class='sbtn' onclick='ts(this)'>" \
        "<span class='sarr'>&#9654;</span>" \
        "<span class='sn'>%s</span>" \
        "<span class='sp'>%s</span>" \
        "<span class='sb' style='color:%s'>%s</span>" \
        "<span class='sc'>%s</span></button>" \
        "<table class='mt'>" \
        "<tr><th>Model</th><th>VID:PID</th><th>Decode</th>" \
        "<th>Notes</th><th>Status</th></tr>", \
        (nm),(pid),(bclr),(badge),(cnt)); \
    strlcat((x),_st,(b)); } while(0)
#define SCLOSE(b,x)  strlcat((x),"</table></div>",(b))
#define MROW(b,x,model,pid,decode,note,stspan) do { \
    char _mt[512]; \
    snprintf(_mt,sizeof(_mt), \
        "<tr><td class='mn'>%s</td><td class='mp'>%s</td>" \
        "<td class='mm'>%s</td><td class='mnt'>%s</td>" \
        "<td>%s</td></tr>", \
        (model),(pid),(decode),(note),(stspan)); \
    strlcat((x),_mt,(b)); } while(0)

#define ST_OK  "<span class='ok'>&#10003; Confirmed</span>"
#define ST_EX  "<span class='ex'>&#9711; Expected</span>"
#define ST_UN  "<span class='un'>&#9711; Unconfirmed</span>"
#define COL_OK "#00c853"
#define COL_EX "#7986cb"
#define COL_UN "#444"

static void render_compat(char *out, size_t outsz) {
    out[0] = 0;
    compat_head(out, outsz);

    /* ═══ APC / Schneider ══════════════════════════════════════════════ */
    VOPEN(outsz, out, "APC / Schneider Electric", "051D:xxxx",
          "&#10003; 2 confirmed", COL_OK, "21 models");

      SOPEN(outsz, out, "Back-UPS (Consumer / SOHO)", "051D:0002",
            "&#10003; 2 confirmed", COL_OK, "15 models");
        MROW(outsz, out, "Back-UPS XS 1500M", "051D:0002", "INT-IN + GET_REPORT",
             "Charge/runtime/status via INT IN. Voltages via rid=0x17.", ST_OK);
        MROW(outsz, out, "Back-UPS BR1000G", "051D:0002", "INT-IN + GET_REPORT",
             "Same VID:PID as XS 1500M. Decode path confirmed working.", ST_OK);
        MROW(outsz, out, "Back-UPS BR700G / BR1500G / BR1500MS2", "051D:0002",
             "INT-IN + GET_REPORT", "Same PID=0002 firmware family.", ST_EX);
        MROW(outsz, out, "Back-UPS Pro USB", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. PID=0002 family.", ST_EX);
        MROW(outsz, out, "Back-UPS BX600M / BX850M / BX1500M / BX****MI", "051D:0002",
             "INT-IN + GET_REPORT", "BX series. NUT-listed. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS BE425M / BE600M1 / BE850M2", "051D:0002",
             "INT-IN + GET_REPORT", "BE series. PID=0002 family.", ST_EX);
        MROW(outsz, out, "Back-UPS BN450M / BN650M1", "051D:0002",
             "INT-IN + GET_REPORT", "BN series. PID=0002 family.", ST_EX);
        MROW(outsz, out, "Back-UPS ES 850G2 / ES/CyberFort 350", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. ES series PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS CS USB / RS USB / LS USB", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. CS/RS/LS PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS BK650M2-CH / BK****M2-CH series", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS BVK****M2 series", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS XS 1000M / BACK-UPS XS LCD", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS BF500", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS CS500", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed as CS500. PID=0002.", ST_EX);
        MROW(outsz, out, "Back-UPS BK650M2-CH / Back-UPS (USB) generic", "051D:0002",
             "INT-IN + GET_REPORT", "NUT-listed. Any Back-UPS with USB and PID=0002.", ST_EX);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Smart-UPS (SMT / SMX / SMC)", "051D:0003+",
            "&#9711; Unconfirmed", COL_UN, "6 models");
        MROW(outsz, out, "Smart-UPS SMT750I / SMT750", "051D:0003", "Standard HID",
             "NUT-listed. Different PID from Back-UPS. Vendor page remap applied.", ST_UN);
        MROW(outsz, out, "Smart-UPS SMT1500I / SMT1000 / SMT2200 / SMT3000", "051D:0003",
             "Standard HID", "NUT-listed SMT family.", ST_UN);
        MROW(outsz, out, "Smart-UPS X SMX750I / SMX1500I", "051D:xxxx",
             "Standard HID", "NUT-listed. SMX series. PID varies.", ST_UN);
        MROW(outsz, out, "Smart-UPS SMC1000 / SMC1500 / SMC2200BI-BR", "051D:xxxx",
             "Standard HID", "SMC2200BI-BR NUT-listed. SMC family expected same path.", ST_UN);
        MROW(outsz, out, "Smart-UPS (USB) generic", "051D:xxxx",
             "Standard HID", "NUT-listed generic Smart-UPS USB.", ST_UN);
        MROW(outsz, out, "Smart-UPS On-Line SRT1000 / SRT2200 / SRT3000", "051D:xxxx",
             "Standard HID", "Double-conversion. USB HID interface not confirmed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ CyberPower ════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "CyberPower (CPS)", "0764:xxxx",
          "&#10003; 1 confirmed", COL_OK, "34 models");

      SOPEN(outsz, out, "AVR / Consumer (PID 0x0501)", "0764:0501",
            "&#10003; 1 confirmed", COL_OK, "22 models");
        MROW(outsz, out, "CP550HG / SX550G", "0764:0501", "Direct bypass INT-IN",
             "All values via rids 0x20-0x88. Descriptor LogMax bug patched.", ST_OK);
        MROW(outsz, out, "CP1200AVR", "0764:0501", "Direct bypass INT-IN",
             "Same PID=0501 decode path. NUT-listed.", ST_EX);
        MROW(outsz, out, "CP825AVR-G / LE825G", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "CP1000AVRLCD / CP1500C", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "CP850PFCLCD / CP1000PFCLCD / CP1350PFCLCD / CP1500PFCLCD",
             "0764:0501", "Direct bypass INT-IN", "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "CP1350AVRLCD / CP1500AVRLCD", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "CP900AVR / CPS685AVR / CPS800AVR", "0764:0501",
             "Direct bypass INT-IN", "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "EC350G / EC750G / EC850LCD", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "BL1250U / AE550 / CPJ500", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "BR1000ELCD", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "CP1350EPFCLCD / CP1500EPFCLCD", "0764:0501",
             "Direct bypass INT-IN", "NUT-listed. PID=0501.", ST_EX);
        MROW(outsz, out, "Value 400E / 600E / 800E / 1500ELCD-RU", "0764:0501",
             "Direct bypass INT-IN", "NUT-listed Value series. PID=0501.", ST_EX);
        MROW(outsz, out, "VP1200ELCD", "0764:0501", "Direct bypass INT-IN",
             "NUT-listed. PID=0501.", ST_EX);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "OR / PR Rackmount (PID 0x0601)", "0764:0601",
            "&#9711; Unconfirmed", COL_UN, "9 models");
        MROW(outsz, out, "OR2200LCDRM2U / OR700LCDRM1U / OR500LCDRM1U / OR1500ERM1U",
             "0764:0601", "Direct bypass INT-IN",
             "Same direct decode as 0x0501. Active power LogMax fix applied.", ST_UN);
        MROW(outsz, out, "PR1500RT2U / PR6000LCDRTXL5U", "0764:0601",
             "Direct bypass INT-IN", "NUT-listed. PID=0601.", ST_UN);
        MROW(outsz, out, "RT650EI / UT2200E", "0764:0601", "Direct bypass INT-IN",
             "NUT-listed. PID=0601.", ST_UN);
        MROW(outsz, out, "CP1350EPFCLCD (0601 variant)", "0764:0601",
             "Direct bypass INT-IN", "NUT-listed under PID=0601.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Legacy (PID 0x0005)", "0764:0005",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "900AVR / BC900D", "0764:0005", "Standard HID",
             "Older model. Standard path with voltage LogMax fix.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Eaton / MGE / Powerware ══════════════════════════════════════ */
    VOPEN(outsz, out, "Eaton / MGE / Powerware", "0463:xxxx",
          "&#9711; Unconfirmed", COL_UN, "18 models");

      SOPEN(outsz, out, "Eaton 3S / 5E / 5P / 5PX / 5SC / 5SX / 9E / 9PX", "0463:xxxx",
            "&#9711; Unconfirmed", COL_UN, "8 models");
        MROW(outsz, out, "3S (USB)", "0463:xxxx", "Standard HID",
             "NUT-listed. Standard HID Power Device class.", ST_UN);
        MROW(outsz, out, "5E (USB)", "0463:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "5P (USB) / 5PX (USB)", "0463:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "5SC (USB) / 5SX (USB)", "0463:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "9E (USB) / 9PX (USB)", "0463:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Ellipse / Evolution / Galaxy / Nova / Pulsar / Powerware",
            "0463:xxxx", "&#9711; Unconfirmed", COL_UN, "10 models");
        MROW(outsz, out, "Ellipse ECO (USB) / Ellipse MAX (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed. MGE subdriver.", ST_UN);
        MROW(outsz, out, "MGE Ellipse Premium (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Evolution 650/850/1150/1550 (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Galaxy 3000/5000 (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed. USB HID interface on these units.", ST_UN);
        MROW(outsz, out, "Nova AVR (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Pulsar EX/EXtreme/M/MX (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Comet EX RT (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Powerware 5110/5115/5125/5130 (USB)", "0463:xxxx",
             "Standard HID", "NUT-listed. Powerware/Eaton branding.", ST_UN);
        MROW(outsz, out, "Powerware 9125/9130/9140/9155/9170/9355 (USB)", "0463:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Tripp Lite ════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Tripp Lite", "09AE:xxxx",
          "&#9711; Unconfirmed", COL_UN, "7 models");

      SOPEN(outsz, out, "SmartPro Series", "09AE:xxxx",
            "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "SMART500RT1U", "09AE:xxxx", "Standard HID + GET_REPORT",
             "NUT-listed. Feature report polling for values not on INT IN.", ST_UN);
        MROW(outsz, out, "SMART700USB", "09AE:xxxx", "Standard HID + GET_REPORT",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "SMART1000LCD / SMART1500LCD / SmartPro 1500LCD", "09AE:xxxx",
             "Standard HID + GET_REPORT", "NUT-listed. SmartPro 1500LCD explicit.", ST_UN);
        MROW(outsz, out, "SMART2200RMXL2U", "09AE:xxxx",
             "Standard HID + GET_REPORT", "NUT-listed. Rackmount SmartPro.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "OmniSmart / INTERNETOFFICE", "09AE:xxxx",
            "&#9711; Unconfirmed", COL_UN, "3 models");
        MROW(outsz, out, "OMNI650LCD / OMNI900LCD / OMNI1000LCD / OMNI1500LCD",
             "09AE:xxxx", "Standard HID + GET_REPORT",
             "NUT-listed as OMNI650/900/1000/1500 LCD.", ST_UN);
        MROW(outsz, out, "INTERNETOFFICE700", "09AE:xxxx",
             "Standard HID + GET_REPORT", "NUT-listed explicitly.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Belkin ════════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Belkin", "050D:xxxx",
          "&#9711; Unconfirmed", COL_UN, "9 models");

      SOPEN(outsz, out, "F6H / F6C / Universal UPS Series", "050D:xxxx",
            "&#9711; Unconfirmed", COL_UN, "9 models");
        MROW(outsz, out, "F6H375-USB", "050D:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "Office Series F6C550-AVR", "050D:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "Regulator PRO-USB", "050D:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "Small Enterprise F6C1500-TW-RK", "050D:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "Universal UPS F6C100-UNV / F6C120-UNV", "050D:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Universal UPS F6C800-UNV / F6C1100-UNV / F6C1200-UNV",
             "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ HP ════════════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "HP", "03F0:xxxx",
          "&#9711; Unconfirmed", COL_UN, "4 models");

      SOPEN(outsz, out, "T Series G2/G3", "03F0:xxxx",
            "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "T750 G2 (USB)",  "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T1000 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T1500 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T3000 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Dell ══════════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Dell", "047C:xxxx",
          "&#9711; Unconfirmed", COL_UN, "4 models");

      SOPEN(outsz, out, "H Series", "047C:xxxx",
            "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "H750E (USB)",  "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H950E (USB)",  "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H1000E (USB)", "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H1750E (USB)", "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Liebert / Vertiv ══════════════════════════════════════════════ */
    VOPEN(outsz, out, "Liebert / Vertiv", "10AF:xxxx",
          "&#9711; Unconfirmed", COL_UN, "2 models");

      SOPEN(outsz, out, "GXT / PSI Series", "10AF:xxxx",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "GXT4 (USB)", "10AF:xxxx", "Standard HID",
             "NUT-listed. Liebert subdriver.", ST_UN);
        MROW(outsz, out, "PSI5 (USB)", "10AF:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Powercom ══════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Powercom", "0D9F:xxxx",
          "&#9711; Unconfirmed", COL_UN, "7 models");

      SOPEN(outsz, out, "All Series", "0D9F:xxxx",
            "&#9711; Unconfirmed", COL_UN, "7 models");
        MROW(outsz, out, "Black Knight Pro (USB)", "0D9F:xxxx", "Standard HID",
             "NUT-listed.", ST_UN);
        MROW(outsz, out, "Dragon (USB)",        "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Imperial (USB)",       "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "King Pro (USB)",       "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Raptor (USB)",         "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Smart King / Smart King Pro (USB)", "0D9F:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "WOW (USB)",            "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Other Vendors ═════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Other Vendors", "various",
          "&#9711; Unconfirmed", COL_UN, "19 manufacturers / 25+ models");

      SOPEN(outsz, out, "AEG Power Solutions", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "PROTECT NAS (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "PROTECT B (USB)",   "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Cyber Energy (ST Micro OEM)", "0483:A430",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "Models with USB", "0483:A430", "Standard HID",
             "NUT-listed. OEM CyberPower variant using ST Micro VID.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Delta", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "Amplon RT Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Amplon N Series (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Dynex", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "DX-800U (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Ecoflow", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "Delta 3 Plus (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "EVER", "unknown",
            "&#9711; Unconfirmed", COL_UN, "3 models");
        MROW(outsz, out, "Sinline RT Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Sinline XL Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "ECO Pro Series (USB)",    "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Geek Squad", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "GS1285U (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "GoldenMate", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "UPS 1000VA Pro (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "IBM", "unknown",
            "&#9711; Unconfirmed", COL_UN, "various");
        MROW(outsz, out, "Various (USB port)", "unknown", "Standard HID",
             "NUT-listed generically. USB HID interface.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "iDowell", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "iBox UPS (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Ippon", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "Back Power Pro (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Smart Power Pro (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Legrand", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "KEOR SP (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "MasterPower", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "MF-UPS650VA (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Minibox", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "openUPS Intelligent UPS (USB)", "unknown",
             "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "PowerWalker", "unknown",
            "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "VI 650 SE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 850 SE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 1000 SE (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 1500 SE (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Powervar", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "ABCE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "ABCEG (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Rocketfish", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "RF-1000VA (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "RF-1025VA (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Salicru", "unknown",
            "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "SPS One Series (USB)",    "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "SPS Xtreme Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Syndome", "unknown",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "TITAN Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* Footer and JS */
    strlcat(out,
        "<div style='font-family:Arial,sans-serif;font-size:0.75em;color:#444;"
             "margin-top:14px;line-height:1.6'>"
        "VID:PID xxxx = any product ID (VID-only wildcard). "
        "Standard HID = generic USB HID Power Device Class. "
        "Direct = vendor-specific byte-position decode. "
        "GET_REPORT = Feature report polling via USB control transfer.<br>"
        "Source: NUT usbhid-ups driver &mdash; "
        "<a href='https://networkupstools.org/stable-hcl.html' style='color:#4fc3f7'>"
        "networkupstools.org/stable-hcl</a></div>"
        "<div class='nav'><a href='/'>Back to Status</a></div>"
        "<script>"
        "function tv(b){b.classList.toggle('open');b.nextElementSibling.classList.toggle('open')}"
        "function ts(b){b.classList.toggle('open');b.nextElementSibling.classList.toggle('open')}"
        "function expandAll(){"
          "document.querySelectorAll('.vbtn,.sbtn').forEach(function(b){"
            "b.classList.add('open');"
            "b.nextElementSibling.classList.add('open');"
          "});"
        "}"
        "function collapseAll(){"
          "document.querySelectorAll('.vbtn,.sbtn').forEach(function(b){"
            "b.classList.remove('open');"
            "b.nextElementSibling.classList.remove('open');"
          "});"
        "}"
        "</script>"
        "</body></html>",
        outsz);
}

/* -------------------------------------------------------------------------
 * Config page   GET /config
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Config page   GET /config
 * ---------------------------------------------------------------------- */

static void render_config(app_cfg_t *cfg, char *out, size_t outsz,
                          const char *note, const char *note_cls) {
    char sta_ip[16];
    wifi_mgr_sta_ip_str(sta_ip);

    bool ap_up = wifi_mgr_ap_is_active();

    char note_html[256] = {0};
    if (note && note[0]) {
        const char *cls = (note_cls && strcmp(note_cls, "err") == 0) ? "note-err" : "note-ok";
        snprintf(note_html, sizeof(note_html), "<div class='%s'>%s</div>", cls, note);
    }

    const char *pw_warn = cfg_store_is_default_pass(cfg)
        ? "<div class='warn'>Default password in use. Change it below under Portal Security.</div>"
        : "";

    snprintf(out, outsz,
        "<!doctype html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>UPS Node Config</title>"
        PORTAL_CSS
        "</head><body>"
        "<h2>ESP32-S3 UPS Node</h2>"
        "<div class='subtitle'>v15.10 &mdash; Configuration</div>"
        "%s%s"
        "<form method='POST' action='/save'>"
        "<div class='form-section'>Wi-Fi (STA)</div>"
        "<div class='form-row'><span class='form-label'>SSID</span>"
            "<input name='sta_ssid' maxlength='32' value='%s'></div>"
        "<div class='form-row'><span class='form-label'>Password</span>"
            "<input name='sta_pass' type='password' maxlength='64' value='%s'></div>"
        "<div class='form-section'>Soft AP &mdash; %s</div>"
        "<div class='form-row'><span class='form-label'>AP SSID</span>"
            "<input name='ap_ssid' maxlength='32' value='%s'></div>"
        "<div class='form-row'><span class='form-label'>AP Password (8+ chars)</span>"
            "<input name='ap_pass' type='password' maxlength='64' value='%s'></div>"
        "<div class='form-section'>NUT Identity</div>"
        "<div class='form-row'><span class='form-label'>UPS Name</span>"
            "<input name='ups_name' maxlength='32' value='%s'></div>"
        "<div class='form-row'><span class='form-label'>NUT Username</span>"
            "<input name='nut_user' maxlength='32' value='%s'></div>"
        "<div class='form-row'><span class='form-label'>NUT Password</span>"
            "<input name='nut_pass' type='password' maxlength='32' value='%s'></div>"
        "<div class='form-section'>Portal Security &mdash; login: admin / &lt;password&gt;</div>"
        "<div class='form-row'><span class='form-label'>New Password</span>"
            "<input name='portal_pass' type='password' maxlength='32' "
            "placeholder='blank = keep current'></div>"
        "<input class='btn' type='submit' value='Save and Apply'>"
        "</form>"
        "<div class='nav' style='margin-top:20px'>"
        "<a href='/'>Back to Status</a>"
        "<a href='/reboot' onclick=\"return confirm('Reboot device?')\">Reboot</a>"
        "<span style='color:#555;font-size:0.82em'>STA: %s</span>"
        "</div>"
        "</body></html>",
        pw_warn, note_html,
        cfg->sta_ssid, cfg->sta_pass,
        ap_up ? "Active" : "Off",
        cfg->ap_ssid, cfg->ap_pass,
        cfg->ups_name, cfg->nut_user, cfg->nut_pass,
        sta_ip[0] ? sta_ip : "not connected"
    );
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
        char *page = (char *)malloc(HTTP_COMPAT_BUF);
        if (page) { render_compat(page, HTTP_COMPAT_BUF); http_send_html(fd, page); free(page); }
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
