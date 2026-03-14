#include "nut_server.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "lwip/sockets.h"

static const char *TAG = "nut_server";
#define NUT_TCP_PORT 3493

typedef struct {
    bool authed;
} nut_session_t;

static void nut_send(int fd, const char *s) {
    if (!s) return;
    send(fd, s, (int)strlen(s), 0);
}

static void nut_sendf(int fd, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    nut_send(fd, buf);
}

static void trim_ws(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n-1]==' ' || s[n-1]=='\t' || s[n-1]=='\r' || s[n-1]=='\n')) s[--n] = 0;
}

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

static void nut_handle_line(app_cfg_t *cfg, int fd, nut_session_t *sess, char *line) {
    for (int i = 0; line[i]; i++) {
        if (line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
    }
    trim_ws(line);
    if (!line[0]) return;

    char *argv[6] = {0};
    int argc = 0;
    char *save = NULL;
    for (char *t = strtok_r(line, " ", &save); t && argc < 6; t = strtok_r(NULL, " ", &save)) {
        argv[argc++] = t;
    }

    if (strcasecmp(argv[0], "VER") == 0) { nut_sendf(fd, "Network UPS Tools upsd-esp32 0.1\n"); return; }
    if (strcasecmp(argv[0], "NETVER") == 0) { nut_sendf(fd, "NETVER 1.0\n"); return; }
    if (strcasecmp(argv[0], "STARTTLS") == 0) { nut_sendf(fd, "ERR FEATURE-NOT-CONFIGURED\n"); return; }

    if (strcasecmp(argv[0], "USERNAME") == 0 && argc >= 2) { nut_sendf(fd, "OK\n"); return; }
    if (strcasecmp(argv[0], "PASSWORD") == 0 && argc >= 2) { sess->authed = true; nut_sendf(fd, "OK\n"); return; }

    if (strcasecmp(argv[0], "LIST") == 0 && argc >= 2 && strcasecmp(argv[1], "UPS") == 0) {
        nut_sendf(fd, "BEGIN LIST UPS\n");
        nut_sendf(fd, "UPS %s \"ESP32S3 USB UPS (HID)\"\n", cfg->ups_name);
        nut_sendf(fd, "END LIST UPS\n");
        return;
    }

    if (strcasecmp(argv[0], "LIST") == 0 && argc >= 3 && strcasecmp(argv[1], "VAR") == 0) {
        const char *ups = argv[2];
        if (strcmp(ups, cfg->ups_name) != 0) { nut_sendf(fd, "ERR UNKNOWN-UPS\n"); return; }

        nut_sendf(fd, "BEGIN LIST VAR %s\n", ups);
        nut_sendf(fd, "VAR %s battery.charge \"99\"\n", ups);
        nut_sendf(fd, "VAR %s battery.runtime \"35640\"\n", ups);
        nut_sendf(fd, "VAR %s input.utility.present \"1\"\n", ups);
        nut_sendf(fd, "VAR %s ups.flags \"0x0000000D\"\n", ups);
        nut_sendf(fd, "VAR %s ups.status \"OL\"\n", ups);
        nut_sendf(fd, "END LIST VAR %s\n", ups);
        return;
    }

    if (strcasecmp(argv[0], "GET") == 0 && argc >= 4 && strcasecmp(argv[1], "VAR") == 0) {
        const char *ups = argv[2];
        const char *var = argv[3];
        if (strcmp(ups, cfg->ups_name) != 0) { nut_sendf(fd, "ERR UNKNOWN-UPS\n"); return; }

        if      (strcmp(var, "battery.charge") == 0)          nut_sendf(fd, "VAR %s %s \"99\"\n", ups, var);
        else if (strcmp(var, "battery.runtime") == 0)         nut_sendf(fd, "VAR %s %s \"35640\"\n", ups, var);
        else if (strcmp(var, "input.utility.present") == 0)   nut_sendf(fd, "VAR %s %s \"1\"\n", ups, var);
        else if (strcmp(var, "ups.flags") == 0)               nut_sendf(fd, "VAR %s %s \"0x0000000D\"\n", ups, var);
        else if (strcmp(var, "ups.status") == 0)              nut_sendf(fd, "VAR %s %s \"OL\"\n", ups, var);
        else                                                  nut_sendf(fd, "ERR UNKNOWN-VAR\n");
        return;
    }

    nut_sendf(fd, "ERR INVALID-COMMAND\n");
}

static void nut_server_task(void *arg) {
    app_cfg_t *cfg = (app_cfg_t*)arg;

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) { ESP_LOGE(TAG, "NUT socket() failed"); vTaskDelete(NULL); return; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NUT_TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) { ESP_LOGE(TAG, "NUT bind() failed"); close(s); vTaskDelete(NULL); return; }
    if (listen(s, 4) != 0) { ESP_LOGE(TAG, "NUT listen() failed"); close(s); vTaskDelete(NULL); return; }

    ESP_LOGI(TAG, "NUT server listening on tcp/%d (plaintext)", NUT_TCP_PORT);

    while (1) {
        struct sockaddr_in6 src;
        socklen_t slen = sizeof(src);
        int c = accept(s, (struct sockaddr *)&src, &slen);
        if (c < 0) continue;

        nut_session_t sess = {0};
        char buf[512];
        int used = 0;

        while (1) {
            int r = recv(c, buf + used, (int)sizeof(buf) - 1 - used, 0);
            if (r <= 0) break;
            used += r;
            buf[used] = 0;

            char *start = buf;
            while (1) {
                char *nl = strchr(start, '\n');
                if (!nl) break;
                *nl = 0;
                char line[256];
                strlcpy0(line, start, sizeof(line));
                nut_handle_line(cfg, c, &sess, line);
                start = nl + 1;
            }

            int rem = (int)strlen(start);
            if (rem > 0 && start != buf) memmove(buf, start, rem + 1);
            used = rem;
            if (used >= (int)sizeof(buf) - 1) used = 0;
        }

        shutdown(c, SHUT_RDWR);
        close(c);
    }
}

void nut_server_start(app_cfg_t *cfg) {
    xTaskCreate(nut_server_task, "nut_srv", 8192, cfg, 5, NULL);
}
