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

static const char *TAG = "http_portal";

#define HTTP_PORT     80
#define HTTP_RX_MAX   8192
#define HTTP_BODY_MAX 2048

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
        "HTTP/1.1 %s\r\n"
        "Connection: close\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        status, ctype, blen
    );
    send(fd, hdr, hlen, 0);
    if (body && blen) send(fd, body, blen, 0);
}

static void http_send_redirect(int fd, const char *location) {
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 303 See Other\r\n"
        "Connection: close\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        location
    );
    send(fd, hdr, hlen, 0);
}

static void http_send_404(int fd) { http_send(fd, "404 Not Found", "text/plain", "404 Not Found"); }
static void http_send_400(int fd, const char *msg) { http_send(fd, "400 Bad Request", "text/plain", msg ? msg : "400 Bad Request"); }
static void http_send_200_html(int fd, const char *html) { http_send(fd, "200 OK", "text/html; charset=utf-8", html ? html : ""); }
static void http_send_200_json(int fd, const char *json) { http_send(fd, "200 OK", "application/json", json ? json : "{}"); }

static void render_config_page(app_cfg_t *cfg, char *out, size_t outsz, const char *note) {
    char ip[16]; wifi_mgr_sta_ip_str(ip);
    if (!note) note = "";

    snprintf(out, outsz,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 UPS</title></head><body>"
        "<h2>ESP32 UPS NUT Node</h2>"
        "<p><b>SoftAP:</b> %s</p>"
        "<p><b>STA IP:</b> %s</p>"
        "<p style='color:#b00;'>%s</p>"
        "<form method='POST' action='/save'>"
        "<h3>Join Local Wi-Fi (STA)</h3>"
        "SSID:<br><input name='sta_ssid' maxlength='32' value='%s'><br>"
        "Password:<br><input name='sta_pass' maxlength='64' value='%s' type='password'><br>"
        "<h3>SoftAP (Setup Portal)</h3>"
        "AP SSID:<br><input name='ap_ssid' maxlength='32' value='%s'><br>"
        "AP Password (8+ chars, blank=open):<br><input name='ap_pass' maxlength='64' value='%s' type='password'><br>"
        "<h3>NUT Identity</h3>"
        "UPS Name:<br><input name='ups_name' maxlength='32' value='%s'><br>"
        "NUT Username:<br><input name='nut_user' maxlength='32' value='%s'><br>"
        "NUT Password:<br><input name='nut_pass' maxlength='32' value='%s' type='password'><br><br>"
        "<button type='submit' name='action' value='save_test'>Save + Test STA Connect</button> "
        "<button type='submit' name='action' value='save_only'>Save Only</button>"
        "</form>"
        "<p><a href='/status'>/status</a> | <a href='/reboot'>Reboot</a></p>"
        "</body></html>",
        WIFI_MGR_SOFTAP_IP_STR, ip, note,
        cfg->sta_ssid, cfg->sta_pass,
        cfg->ap_ssid, cfg->ap_pass,
        cfg->ups_name, cfg->nut_user, cfg->nut_pass
    );
}

static void parse_form_kv(app_cfg_t *cfg_inout, const char *body, char *action_out, size_t action_sz) {
    if (action_out && action_sz) action_out[0] = 0;

    char tmp[HTTP_BODY_MAX + 1];
    strlcpy0(tmp, body ? body : "", sizeof(tmp));

    char *saveptr = NULL;
    for (char *tok = strtok_r(tmp, "&", &saveptr); tok; tok = strtok_r(NULL, "&", &saveptr)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = tok;
        char *v = eq + 1;
        url_decode_inplace(k);
        url_decode_inplace(v);
        trim_ws(k);
        if (strcmp(k, "sta_ssid") == 0) strlcpy0(cfg_inout->sta_ssid, v, sizeof(cfg_inout->sta_ssid));
        else if (strcmp(k, "sta_pass") == 0) strlcpy0(cfg_inout->sta_pass, v, sizeof(cfg_inout->sta_pass));
        else if (strcmp(k, "ap_ssid") == 0) strlcpy0(cfg_inout->ap_ssid, v, sizeof(cfg_inout->ap_ssid));
        else if (strcmp(k, "ap_pass") == 0) strlcpy0(cfg_inout->ap_pass, v, sizeof(cfg_inout->ap_pass));
        else if (strcmp(k, "ups_name") == 0) strlcpy0(cfg_inout->ups_name, v, sizeof(cfg_inout->ups_name));
        else if (strcmp(k, "nut_user") == 0) strlcpy0(cfg_inout->nut_user, v, sizeof(cfg_inout->nut_user));
        else if (strcmp(k, "nut_pass") == 0) strlcpy0(cfg_inout->nut_pass, v, sizeof(cfg_inout->nut_pass));
        else if (strcmp(k, "action") == 0 && action_out && action_sz) strlcpy0(action_out, v, action_sz);
    }
}

static void handle_http_client(app_cfg_t *cfg, int fd) {
    char rx[HTTP_RX_MAX + 1];
    int n = sock_read_until(fd, rx, sizeof(rx), "\r\n\r\n");
    if (n <= 0) { close(fd); return; }

    char *line_end = strstr(rx, "\r\n");
    if (!line_end) { http_send_400(fd, "Bad request"); close(fd); return; }
    *line_end = 0;

    char method[8] = {0}, uri[256] = {0};
    if (sscanf(rx, "%7s %255s", method, uri) != 2) { http_send_400(fd, "Bad request line"); close(fd); return; }

    char path[256];
    strlcpy0(path, uri, sizeof(path));
    char *qmark = strchr(path, '?');
    const char *query = "";
    if (qmark) { *qmark = 0; query = qmark + 1; }

    char *headers = line_end + 2;
    char *hdr_end = strstr(headers, "\r\n\r\n");
    if (!hdr_end) { http_send_400(fd, "Bad headers"); close(fd); return; }
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
        if (content_len > HTTP_BODY_MAX) { http_send_400(fd, "Body too large"); close(fd); return; }
        if (content_len > already) {
            int remaining = content_len - already;
            while (remaining > 0) {
                int r = recv(fd, rx + n, (int)sizeof(rx) - 1 - n, 0);
                if (r <= 0) break;
                n += r;
                rx[n] = 0;
                remaining -= r;
            }
            hdr_end = strstr(rx, "\r\n\r\n");
            if (!hdr_end) { http_send_400(fd, "Bad request"); close(fd); return; }
            body = hdr_end + 4;
        }
        if (content_len >= 0 && (int)strlen(body) > content_len) body[content_len] = 0;
    }

    if (strcmp(path, "/") == 0) {
        http_send_redirect(fd, "/config");
    } else if (strcmp(path, "/config") == 0 && strcasecmp(method, "GET") == 0) {
        char page[4096];
        render_config_page(cfg, page, sizeof(page), "");
        http_send_200_html(fd, page);
    } else if (strcmp(path, "/status") == 0 && strcasecmp(method, "GET") == 0) {
        char ip[16]; wifi_mgr_sta_ip_str(ip);
        char json[512];
        snprintf(json, sizeof(json),
                 "{\"ap_ssid\":\"%s\",\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\",\"ups_name\":\"%s\",\"nut_port\":%d}",
                 cfg->ap_ssid, cfg->sta_ssid, ip, cfg->ups_name, 3493);
        http_send_200_json(fd, json);
    } else if (strcmp(path, "/save") == 0 && (strcasecmp(method, "POST") == 0 || strcasecmp(method, "GET") == 0)) {
        const char *payload = (strcasecmp(method, "POST") == 0) ? body : query;

        app_cfg_t newcfg = *cfg;
        char action[32];
        parse_form_kv(&newcfg, payload, action, sizeof(action));

        if (!is_printable_ascii(newcfg.ap_ssid) || strlen(newcfg.ap_ssid) == 0) {
            char page[4096];
            render_config_page(cfg, page, sizeof(page), "Invalid AP SSID.");
            http_send_200_html(fd, page);
            close(fd);
            return;
        }

        *cfg = newcfg;
        esp_err_t err = cfg_store_commit(cfg);

        char note[256] = {0};
        if (err != ESP_OK) snprintf(note, sizeof(note), "Save failed: %s", esp_err_to_name(err));
        else snprintf(note, sizeof(note), "Saved OK.");

        (void)action;

        char page[4096];
        render_config_page(cfg, page, sizeof(page), note);
        http_send_200_html(fd, page);
    } else if (strcmp(path, "/reboot") == 0 && strcasecmp(method, "GET") == 0) {
        http_send(fd, "200 OK", "text/plain", "Rebooting...\n");
        shutdown(fd, SHUT_RDWR);
        close(fd);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;
    } else {
        http_send_404(fd);
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);
}

static void http_server_task(void *arg) {
    app_cfg_t *cfg = (app_cfg_t*)arg;

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) { ESP_LOGE(TAG, "HTTP socket() failed"); vTaskDelete(NULL); return; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) { ESP_LOGE(TAG, "HTTP bind() failed"); close(s); vTaskDelete(NULL); return; }
    if (listen(s, 4) != 0) { ESP_LOGE(TAG, "HTTP listen() failed"); close(s); vTaskDelete(NULL); return; }

    ESP_LOGI(TAG, "HTTP server listening on :%d (AP: http://%s/config, STA: http://<STA-IP>/config)", HTTP_PORT, WIFI_MGR_SOFTAP_IP_STR);

    while (1) {
        struct sockaddr_in6 src;
        socklen_t slen = sizeof(src);
        int c = accept(s, (struct sockaddr *)&src, &slen);
        if (c < 0) continue;
        handle_http_client(cfg, c);
    }
}

void http_portal_start(app_cfg_t *cfg) {
    xTaskCreate(http_server_task, "http_srv", 16384, cfg, 5, NULL);
}
