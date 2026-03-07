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
#define HTTP_PAGE_BUF 4096

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
 * Static table rendered server-side on first load.
 * A small inline <script> then polls /status every 5 seconds and updates
 * only the live data cells — no page reload, no polling when tab is closed.
 *
 * Cell IDs polled from /status JSON:
 *   td_status  <- ups_status
 *   td_charge  <- battery_charge
 *   td_ip      <- sta_ip
 *
 * All other fields (mfr, model, voltages, load) come from the initial
 * server render. They change rarely and update on next manual refresh.
 * Adding more live fields is trivial — extend the JS updateCells() function.
 * ---------------------------------------------------------------------- */

static void render_dashboard(app_cfg_t *cfg, char *out, size_t outsz) {
    ups_state_t ups;
    ups_state_snapshot(&ups);

    const char *st = ups.ups_status[0] ? ups.ups_status : "UNKNOWN";

    char s_mfr[80], s_model[80];
    char s_charge[20], s_runtime[24], s_bvolt[20];
    char s_ivolt[20],  s_ovolt[20],   s_load[20];

    strlcpy0(s_mfr,   (ups.valid && ups.manufacturer[0]) ? ups.manufacturer : "-", sizeof(s_mfr));
    strlcpy0(s_model, (ups.valid && ups.product[0])      ? ups.product      : "-", sizeof(s_model));

    if (ups.valid && ups.battery_charge > 0)
        snprintf(s_charge, sizeof(s_charge), "%u%%", ups.battery_charge);
    else strlcpy0(s_charge, "-", sizeof(s_charge));

    if (ups.valid && ups.battery_runtime_valid)
        snprintf(s_runtime, sizeof(s_runtime), "%lu s", (unsigned long)ups.battery_runtime_s);
    else strlcpy0(s_runtime, "-", sizeof(s_runtime));

    if (ups.valid && ups.battery_voltage_valid)
        snprintf(s_bvolt, sizeof(s_bvolt), "%.3f V", ups.battery_voltage_mv / 1000.0f);
    else strlcpy0(s_bvolt, "-", sizeof(s_bvolt));

    if (ups.valid && ups.input_voltage_valid)
        snprintf(s_ivolt, sizeof(s_ivolt), "%.1f V", ups.input_voltage_mv / 1000.0f);
    else strlcpy0(s_ivolt, "-", sizeof(s_ivolt));

    if (ups.valid && ups.output_voltage_valid)
        snprintf(s_ovolt, sizeof(s_ovolt), "%.1f V", ups.output_voltage_mv / 1000.0f);
    else strlcpy0(s_ovolt, "-", sizeof(s_ovolt));

    if (ups.valid && ups.ups_load_valid)
        snprintf(s_load, sizeof(s_load), "%u%%", ups.ups_load_pct);
    else strlcpy0(s_load, "-", sizeof(s_load));

    char sta_ip[16];
    wifi_mgr_sta_ip_str(sta_ip);

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
        "<h2>ESP32-S3 UPS Node v14.25</h2>"
        "%s"
        "<table border='1' cellpadding='4' cellspacing='0'>"
        "<tr><th>Manufacturer</th><td>%s</td></tr>"
        "<tr><th>Model</th><td>%s</td></tr>"
        "<tr><th>Driver</th><td>esp32-nut-hid</td></tr>"
        "<tr><th>Status</th><td id='td_status'><b>%s</b></td></tr>"
        "<tr><th>Charge</th><td id='td_charge'>%s</td></tr>"
        "<tr><th>Runtime</th><td>%s</td></tr>"
        "<tr><th>Batt Voltage</th><td>%s</td></tr>"
        "<tr><th>Input Voltage</th><td>%s</td></tr>"
        "<tr><th>Output Voltage</th><td>%s</td></tr>"
        "<tr><th>Load</th><td>%s</td></tr>"
        "<tr><th>STA IP</th><td id='td_ip'>%s</td></tr>"
        "</table>"
        "<br><small id='td_poll'>Polling every 5s</small>"
        "<br><br><a href='/config'>[Configure]</a>"
        " &nbsp; <a href='/status'>[Status JSON]</a>"
        "<script>"
        "var t=setInterval(function(){"
          "var x=new XMLHttpRequest();"
          "x.open('GET','/status',true);"
          "x.onload=function(){"
            "if(x.status===200){"
              "try{"
                "var d=JSON.parse(x.responseText);"
                "document.getElementById('td_status').innerHTML='<b>'+d.ups_status+'</b>';"
                "document.getElementById('td_charge').textContent=d.battery_charge+'%%';"
                "document.getElementById('td_ip').textContent=d.sta_ip;"
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
        st, s_charge, s_runtime,
        s_bvolt, s_ivolt, s_ovolt, s_load,
        sta_ip[0] ? sta_ip : "not connected"
    );

    (void)cfg;
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
        "<h2>ESP32-S3 UPS Node v14.25 - Config</h2>"
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

    /* Auth: /status always open */
    bool needs_auth = (strcmp(path, "/status") != 0);
    if (needs_auth && !check_auth(cfg, headers, hdr_end)) {
        http_send_auth_required(fd);
        free(rx); socket_close_graceful(fd); return;
    }

    /* ---- Route dispatch ---- */

    if (strcmp(path, "/") == 0 && strcasecmp(method, "GET") == 0) {
        char *page = (char *)malloc(HTTP_PAGE_BUF);
        if (page) { render_dashboard(cfg, page, HTTP_PAGE_BUF); http_send_html(fd, page); free(page); }
        else       { http_send(fd, "500 Internal Server Error", "text/plain", "OOM"); }

    } else if ((strcmp(path, "/config") == 0 || strcmp(path, "/config/") == 0)
               && strcasecmp(method, "GET") == 0) {
        char *page = (char *)malloc(HTTP_PAGE_BUF);
        if (page) { render_config(cfg, page, HTTP_PAGE_BUF, NULL, NULL); http_send_html(fd, page); free(page); }
        else       { http_send(fd, "500 Internal Server Error", "text/plain", "OOM"); }

    } else if (strcmp(path, "/status") == 0 && strcasecmp(method, "GET") == 0) {
        char sta_ip[16];
        wifi_mgr_sta_ip_str(sta_ip);
        ups_state_t ups;
        ups_state_snapshot(&ups);
        char json[512];
        snprintf(json, sizeof(json),
            "{\"ap_ssid\":\"%s\",\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\","
            "\"ups_name\":\"%s\",\"nut_port\":3493,"
            "\"ups_status\":\"%s\",\"battery_charge\":%u,"
            "\"ups_valid\":%s,\"ap_active\":%s}",
            cfg->ap_ssid, cfg->sta_ssid, sta_ip,
            cfg->ups_name,
            ups.ups_status[0] ? ups.ups_status : "UNKNOWN",
            ups.battery_charge,
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
