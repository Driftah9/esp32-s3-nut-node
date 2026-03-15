/*============================================================================
 MODULE: http_portal

 RESPONSIBILITY
 - Raw-socket HTTP server: routing, auth, transport helpers
 - GET  /         -> render_dashboard() [http_dashboard.c]
 - GET  /config   -> render_config()    [http_config_page.c]
 - POST /save     -> parse_form_kv()    [http_config_page.c]
 - GET  /status   -> JSON snapshot (inline — small, no helper needed)
 - GET  /compat   -> render_compat()    [http_compat.c]
 - GET  /reboot   -> esp_restart()

 REVERT HISTORY
 R0   v14.7   modular baseline
 R17  v15.10  Dark theme, AJAX, CyberPower OB fix, poll clock
 R18  v15.11  Split render_dashboard -> http_dashboard.c
              Split render_config + parse_form_kv -> http_config_page.c
              Remove duplicate render_compat (lives in http_compat.c)
              Remove inline PORTAL_CSS (lives in http_portal_css.h)
              All fixes applied: AJAX ID mismatch, CP rid=0x21 runtime,
              Smart-UPS C uid cache additions, wall clock
 R19  v15.13  /status JSON expanded: DB static fields added
              (battery_voltage_nominal_v, battery_runtime_low_s,
              battery_charge_low, battery_charge_warning,
              input_voltage_nominal_v, ups_type, ups_firmware,
              device_mfr, device_model, device_serial, driver_version)
              Added ups_device_db.h include for DB lookup on /status.

============================================================================*/

#include "http_portal.h"
#include "http_dashboard.h"
#include "http_config_page.h"
#include "http_compat.h"

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
#include "ups_device_db.h"

static const char *TAG = "http_portal";

#define HTTP_PORT     80
#define HTTP_RX_MAX   4096
#define HTTP_BODY_MAX 2048

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
 * Socket helpers
 * ---------------------------------------------------------------------- */

static void socket_close_graceful(int fd) {
    shutdown(fd, SHUT_WR);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char drain[32];
    while (recv(fd, drain, sizeof(drain), 0) > 0) {}
    close(fd);
}

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

/* -------------------------------------------------------------------------
 * HTTP transport
 * ---------------------------------------------------------------------- */

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
        char sta_ip[16];
        wifi_mgr_sta_ip_str(sta_ip);
        ups_state_t ups;
        ups_state_snapshot(&ups);

        /* Look up DB entry for static NUT fields */
        const ups_device_entry_t *db = ups_device_db_lookup(ups.vid, ups.pid);

        char json[1024];
        char bvolt_s[20], load_s[12], runtime_s[12], ivolt_s[20], ovolt_s[20];
        char bvolt_nom_s[20], runtime_low_s[12], ivolt_nom_s[12];

        if (ups.battery_voltage_valid)
            snprintf(bvolt_s, sizeof(bvolt_s), "%.3f", ups.battery_voltage_mv / 1000.0f);
        else strlcpy0(bvolt_s, "null", sizeof(bvolt_s));

        if (ups.ups_load_valid)
            snprintf(load_s, sizeof(load_s), "%u", ups.ups_load_pct);
        else strlcpy0(load_s, "null", sizeof(load_s));

        if (ups.battery_runtime_valid)
            snprintf(runtime_s, sizeof(runtime_s), "%lu", (unsigned long)ups.battery_runtime_s);
        else strlcpy0(runtime_s, "null", sizeof(runtime_s));

        if (ups.input_voltage_valid)
            snprintf(ivolt_s, sizeof(ivolt_s), "%.3f", ups.input_voltage_mv / 1000.0f);
        else strlcpy0(ivolt_s, "null", sizeof(ivolt_s));

        if (ups.output_voltage_valid)
            snprintf(ovolt_s, sizeof(ovolt_s), "%.3f", ups.output_voltage_mv / 1000.0f);
        else strlcpy0(ovolt_s, "null", sizeof(ovolt_s));

        /* Static DB fields — null if unknown */
        if (db && db->battery_voltage_nominal_mv)
            snprintf(bvolt_nom_s, sizeof(bvolt_nom_s), "%.1f", db->battery_voltage_nominal_mv / 1000.0f);
        else strlcpy0(bvolt_nom_s, "null", sizeof(bvolt_nom_s));

        if (db && db->battery_runtime_low_s)
            snprintf(runtime_low_s, sizeof(runtime_low_s), "%lu", (unsigned long)db->battery_runtime_low_s);
        else strlcpy0(runtime_low_s, "null", sizeof(runtime_low_s));

        if (db && db->input_voltage_nominal_v)
            snprintf(ivolt_nom_s, sizeof(ivolt_nom_s), "%u", db->input_voltage_nominal_v);
        else strlcpy0(ivolt_nom_s, "null", sizeof(ivolt_nom_s));

        snprintf(json, sizeof(json),
            "{"
            "\"ap_ssid\":\"%s\","
            "\"sta_ssid\":\"%s\","
            "\"sta_ip\":\"%s\","
            "\"ups_name\":\"%s\","
            "\"nut_port\":3493,"
            "\"ups_status\":\"%s\","
            "\"battery_charge\":%u,"
            "\"battery_charge_low\":%u,"
            "\"battery_charge_warning\":%u,"
            "\"battery_runtime_s\":%s,"
            "\"battery_runtime_low_s\":%s,"
            "\"battery_voltage_v\":%s,"
            "\"battery_voltage_nominal_v\":%s,"
            "\"ups_load_pct\":%s,"
            "\"input_voltage_v\":%s,"
            "\"input_voltage_nominal_v\":%s,"
            "\"output_voltage_v\":%s,"
            "\"ups_type\":\"%s\","
            "\"ups_firmware\":\"%s\","
            "\"device_mfr\":\"%s\","
            "\"device_model\":\"%s\","
            "\"device_serial\":\"%s\","
            "\"driver_version\":\"15.13\","
            "\"ups_valid\":%s,"
            "\"ap_active\":%s"
            "}",
            cfg->ap_ssid, cfg->sta_ssid, sta_ip, cfg->ups_name,
            ups.ups_status[0] ? ups.ups_status : "UNKNOWN",
            ups.battery_charge,
            db ? (unsigned)db->battery_charge_low    : 10u,
            db ? (unsigned)db->battery_charge_warning : 50u,
            runtime_s, runtime_low_s,
            bvolt_s, bvolt_nom_s,
            load_s,
            ivolt_s, ivolt_nom_s,
            ovolt_s,
            (db && db->ups_type) ? db->ups_type : "line-interactive",
            ups.ups_firmware[0] ? ups.ups_firmware : "unknown",
            ups.manufacturer[0] ? ups.manufacturer : "unknown",
            ups.product[0]      ? ups.product      : "unknown",
            ups.serial[0]       ? ups.serial        : "unknown",
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
