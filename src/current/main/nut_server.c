/*============================================================================
 MODULE: nut_server

 RESPONSIBILITY
 - Plaintext NUT-compatible TCP server
 - Exposes live UPS data from ups_state
 - Exposes device identity metadata from USB strings

 REVERT HISTORY
 R0  v14.7 LAN interop baseline
 R1  v14.7 modular + ups_state integration
 R2  v14.9 expanded variable exposure
 R3  v14.10 serves candidate metric variables when validated
 R4  v14.12 device.mfr / device.model / device.serial / driver.name
 R5  v14.16 NUT banner on connect
 R6  v14.17 SO_RCVTIMEO (5s)
 R7  v14.18 suppress connect-time banner
 R8  v14.20 close-after-LIST-VAR
 R9  v14.23 gate battery.runtime on battery_runtime_valid
 R10 v14.24 add standard NUT variables for broad APC HID compatibility:
            - battery.charge.low  (per-model threshold from ups_state)
            - battery.type        "PbAc" (static — all APC Back-UPS sealed lead)
            - ups.type            "line-interactive" (static — all Back-UPS)
            - ups.firmware        extracted from product string at enumeration
            - driver.version      static version string
            - ups.vendorid / ups.productid  (from USB identity)
            compound ups.status now produced by ups_hid_parser:
            "OL", "OL CHRG", "OL LB", "OB DISCHRG", "OB DISCHRG LB"
 R11 v15.3  driver.version bumped to 15.3 to match firmware version.
 R12 v15.7  Remove input.voltage and output.voltage NUT variables.
 R13 v15.8  Re-add input.voltage and output.voltage — populated by GET_REPORT.
            Add F9 static NUT diagnostic vars:
              battery.type      (static: "PbAc")
              battery.charge.low already existed
              device.type       (static: "ups")
            DRIVER_VERSION bumped to 15.8.
 R14 v15.12 Full NUT variable parity — serve all standard NUT variables
            that real usbhid-ups exposes for confirmed devices:
              battery.voltage.nominal   (from DB per device)
              battery.runtime.low       (from DB per device)
              battery.charge.warning    (from DB per device)
              input.voltage.nominal     (from DB per device)
              ups.type                  (from DB per device)
              ups.test.result           (static: No test initiated)
              ups.delay.shutdown        (static: 20)
              ups.delay.start           (static: 30)
              ups.timer.reboot          (static: -1)
              ups.timer.shutdown        (static: -1)
            DRIVER_VERSION bumped to 15.12.

============================================================================*/

#include "nut_server.h"
#include "ups_state.h"
#include "ups_device_db.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "nut_server";
static const char *DRIVER_NAME    = "esp32-nut-hid";
static const char *DRIVER_VERSION = "15.15";
static const char *DEVICE_TYPE    = "ups";              /* standard NUT device.type value */
static const char *BATTERY_TYPE   = "PbAc";          /* sealed lead-acid — all APC Back-UPS */
static const char *UPS_TYPE       = "line-interactive"; /* all Back-UPS series */

#define NUT_TCP_PORT        3493
#define NUT_RECV_TIMEOUT_S  5
#define NUT_SEND_TIMEOUT_S  5

typedef struct {
    bool authed;
    bool close_after;
} nut_session_t;

static void nut_send(int fd, const char *s)
{
    if (!s) return;
    send(fd, s, (int)strlen(s), 0);
}

static void nut_sendf(int fd, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    nut_send(fd, buf);
}

static void trim_ws(char *s)
{
    if (!s) return;
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = 0;
    }
}

static void strlcpy0(char *dst, const char *src, size_t dstsz)
{
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

static const char *safe_or_unknown(const char *s)
{
    return (s && s[0]) ? s : "UNKNOWN";
}

static void emit_var_list(int fd, const char *ups, const ups_state_t *st)
{
    /* Look up device DB entry for static NUT variables */
    const ups_device_entry_t *db = ups_device_db_lookup(st->vid, st->pid);

    /* Determine ups.type: DB entry overrides static default */
    const char *ups_type_str = (db && db->ups_type) ? db->ups_type : UPS_TYPE;

    /* --- Battery --- */
    nut_sendf(fd, "VAR %s battery.charge \"%u\"\n",     ups, (unsigned)st->battery_charge);
    nut_sendf(fd, "VAR %s battery.charge.low \"%u\"\n",
              ups, db ? (unsigned)db->battery_charge_low : (unsigned)st->battery_charge_low);
    if (db && db->battery_charge_warning) {
        nut_sendf(fd, "VAR %s battery.charge.warning \"%u\"\n",
                  ups, (unsigned)db->battery_charge_warning);
    }
    nut_sendf(fd, "VAR %s battery.type \"%s\"\n", ups, BATTERY_TYPE);
    if (st->battery_runtime_valid) {
        nut_sendf(fd, "VAR %s battery.runtime \"%u\"\n", ups, (unsigned)st->battery_runtime_s);
    }
    if (db && db->battery_runtime_low_s) {
        nut_sendf(fd, "VAR %s battery.runtime.low \"%u\"\n",
                  ups, (unsigned)db->battery_runtime_low_s);
    }
    if (st->battery_voltage_valid) {
        nut_sendf(fd, "VAR %s battery.voltage \"%.3f\"\n", ups,
                  (double)st->battery_voltage_mv / 1000.0);
    }
    if (db && db->battery_voltage_nominal_mv) {
        nut_sendf(fd, "VAR %s battery.voltage.nominal \"%.1f\"\n",
                  ups, (double)db->battery_voltage_nominal_mv / 1000.0);
    }

    /* --- Input / Output --- */
    nut_sendf(fd, "VAR %s input.utility.present \"%u\"\n", ups,
              st->input_utility_present ? 1u : 0u);
    if (st->input_voltage_valid) {
        nut_sendf(fd, "VAR %s input.voltage \"%.1f\"\n", ups,
                  (double)st->input_voltage_mv / 1000.0);
    }
    if (db && db->input_voltage_nominal_v) {
        nut_sendf(fd, "VAR %s input.voltage.nominal \"%u\"\n",
                  ups, (unsigned)db->input_voltage_nominal_v);
    }
    if (st->output_voltage_valid) {
        nut_sendf(fd, "VAR %s output.voltage \"%.1f\"\n", ups,
                  (double)st->output_voltage_mv / 1000.0);
    }

    /* --- UPS status and metrics --- */
    nut_sendf(fd, "VAR %s ups.status \"%s\"\n",  ups,
              st->ups_status[0] ? st->ups_status : "UNKNOWN");
    nut_sendf(fd, "VAR %s ups.flags \"0x%08X\"\n", ups, (unsigned)st->ups_flags);
    nut_sendf(fd, "VAR %s ups.type \"%s\"\n",    ups, ups_type_str);
    nut_sendf(fd, "VAR %s device.type \"%s\"\n", ups, DEVICE_TYPE);
    if (st->ups_firmware[0]) {
        nut_sendf(fd, "VAR %s ups.firmware \"%s\"\n", ups, st->ups_firmware);
    }
    if (st->ups_load_valid) {
        nut_sendf(fd, "VAR %s ups.load \"%u\"\n", ups, (unsigned)st->ups_load_pct);
    }
    /* Standard NUT timer/test vars — expected by NUT clients (apcupsd-compat, HA) */
    nut_sendf(fd, "VAR %s ups.test.result \"No test initiated\"\n", ups);
    nut_sendf(fd, "VAR %s ups.delay.shutdown \"20\"\n",   ups);
    nut_sendf(fd, "VAR %s ups.delay.start \"30\"\n",      ups);
    nut_sendf(fd, "VAR %s ups.timer.reboot \"-1\"\n",     ups);
    nut_sendf(fd, "VAR %s ups.timer.shutdown \"-1\"\n",   ups);

    /* --- Device identity --- */
    nut_sendf(fd, "VAR %s device.mfr \"%s\"\n",    ups, safe_or_unknown(st->manufacturer));
    nut_sendf(fd, "VAR %s device.model \"%s\"\n",  ups, safe_or_unknown(st->product));
    nut_sendf(fd, "VAR %s device.serial \"%s\"\n", ups, safe_or_unknown(st->serial));
    nut_sendf(fd, "VAR %s ups.mfr \"%s\"\n",       ups, safe_or_unknown(st->manufacturer));
    nut_sendf(fd, "VAR %s ups.model \"%s\"\n",     ups, safe_or_unknown(st->product));

    /* --- Driver / USB info --- */
    nut_sendf(fd, "VAR %s driver.name \"%s\"\n",      ups, DRIVER_NAME);
    nut_sendf(fd, "VAR %s driver.version \"%s\"\n",   ups, DRIVER_VERSION);
    if (st->vid) {
        nut_sendf(fd, "VAR %s ups.vendorid \"%04x\"\n",  ups, (unsigned)st->vid);
        nut_sendf(fd, "VAR %s ups.productid \"%04x\"\n", ups, (unsigned)st->pid);
    }
}

static void nut_handle_line(app_cfg_t *cfg, int fd, nut_session_t *sess, char *line)
{
    ups_state_t st;
    ups_state_snapshot(&st);
    (void)sess;

    for (int i = 0; line[i]; i++) {
        if (line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
    }
    trim_ws(line);
    if (!line[0]) return;

    ESP_LOGD(TAG, "CMD: '%s'", line);

    char *argv[6] = {0};
    int argc = 0;
    char *save = NULL;
    for (char *t = strtok_r(line, " ", &save); t && argc < 6; t = strtok_r(NULL, " ", &save)) {
        argv[argc++] = t;
    }

    if (strcasecmp(argv[0], "VER") == 0) {
        nut_sendf(fd, "Network UPS Tools upsd-esp32 %s\n", DRIVER_VERSION);
        return;
    }
    if (strcasecmp(argv[0], "NETVER") == 0) {
        nut_sendf(fd, "NETVER 1.0\n");
        return;
    }
    if (strcasecmp(argv[0], "STARTTLS") == 0) {
        ESP_LOGI(TAG, "STARTTLS rejected (plaintext only)");
        nut_sendf(fd, "ERR FEATURE-NOT-CONFIGURED\n");
        return;
    }
    if (strcasecmp(argv[0], "USERNAME") == 0 && argc >= 2) {
        nut_sendf(fd, "OK\n");
        return;
    }
    if (strcasecmp(argv[0], "PASSWORD") == 0 && argc >= 2) {
        sess->authed = true;
        nut_sendf(fd, "OK\n");
        return;
    }

    if (strcasecmp(argv[0], "LIST") == 0 && argc >= 2 && strcasecmp(argv[1], "UPS") == 0) {
        nut_sendf(fd, "BEGIN LIST UPS\n");
        nut_sendf(fd, "UPS %s \"%s\"\n", cfg->ups_name, safe_or_unknown(st.product));
        nut_sendf(fd, "END LIST UPS\n");
        return;
    }

    if (strcasecmp(argv[0], "LIST") == 0 && argc >= 3 && strcasecmp(argv[1], "VAR") == 0) {
        const char *ups = argv[2];
        if (strcmp(ups, cfg->ups_name) != 0) {
            nut_sendf(fd, "ERR UNKNOWN-UPS\n");
            return;
        }
        nut_sendf(fd, "BEGIN LIST VAR %s\n", ups);
        emit_var_list(fd, ups, &st);
        nut_sendf(fd, "END LIST VAR %s\n", ups);
        nut_sendf(fd, "OK Goodbye\n");
        sess->close_after = true;
        return;
    }

    if (strcasecmp(argv[0], "GET") == 0 && argc >= 4 && strcasecmp(argv[1], "VAR") == 0) {
        const char *ups = argv[2];
        const char *var = argv[3];
        if (strcmp(ups, cfg->ups_name) != 0) {
            nut_sendf(fd, "ERR UNKNOWN-UPS\n");
            return;
        }

        /* Battery */
        if      (strcmp(var, "battery.charge") == 0)
            nut_sendf(fd, "VAR %s %s \"%u\"\n", ups, var, (unsigned)st.battery_charge);
        else if (strcmp(var, "battery.charge.low") == 0)
            nut_sendf(fd, "VAR %s %s \"%u\"\n", ups, var, (unsigned)st.battery_charge_low);
        else if (strcmp(var, "battery.type") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, BATTERY_TYPE);
        else if (strcmp(var, "battery.runtime") == 0 && st.battery_runtime_valid)
            nut_sendf(fd, "VAR %s %s \"%u\"\n", ups, var, (unsigned)st.battery_runtime_s);
        else if (strcmp(var, "battery.voltage") == 0 && st.battery_voltage_valid)
            nut_sendf(fd, "VAR %s %s \"%.3f\"\n", ups, var, (double)st.battery_voltage_mv / 1000.0);
        /* Input / Output */
        else if (strcmp(var, "input.utility.present") == 0)
            nut_sendf(fd, "VAR %s %s \"%u\"\n", ups, var, st.input_utility_present ? 1u : 0u);
        else if (strcmp(var, "input.voltage") == 0 && st.input_voltage_valid)
            nut_sendf(fd, "VAR %s %s \"%.1f\"\n", ups, var, (double)st.input_voltage_mv / 1000.0);
        else if (strcmp(var, "output.voltage") == 0 && st.output_voltage_valid)
            nut_sendf(fd, "VAR %s %s \"%.1f\"\n", ups, var, (double)st.output_voltage_mv / 1000.0);
        /* UPS status */
        else if (strcmp(var, "ups.status") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var,
                      st.ups_status[0] ? st.ups_status : "UNKNOWN");
        else if (strcmp(var, "ups.flags") == 0)
            nut_sendf(fd, "VAR %s %s \"0x%08X\"\n", ups, var, (unsigned)st.ups_flags);
        else if (strcmp(var, "ups.type") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, UPS_TYPE);
        else if (strcmp(var, "device.type") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, DEVICE_TYPE);
        else if (strcmp(var, "ups.firmware") == 0 && st.ups_firmware[0])
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, st.ups_firmware);
        else if (strcmp(var, "ups.load") == 0 && st.ups_load_valid)
            nut_sendf(fd, "VAR %s %s \"%u\"\n", ups, var, (unsigned)st.ups_load_pct);
        /* Identity */
        else if (strcmp(var, "device.mfr") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, safe_or_unknown(st.manufacturer));
        else if (strcmp(var, "device.model") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, safe_or_unknown(st.product));
        else if (strcmp(var, "device.serial") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, safe_or_unknown(st.serial));
        else if (strcmp(var, "ups.mfr") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, safe_or_unknown(st.manufacturer));
        else if (strcmp(var, "ups.model") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, safe_or_unknown(st.product));
        /* Driver / USB */
        else if (strcmp(var, "driver.name") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, DRIVER_NAME);
        else if (strcmp(var, "driver.version") == 0)
            nut_sendf(fd, "VAR %s %s \"%s\"\n", ups, var, DRIVER_VERSION);
        else if (strcmp(var, "ups.vendorid") == 0 && st.vid)
            nut_sendf(fd, "VAR %s %s \"%04x\"\n", ups, var, (unsigned)st.vid);
        else if (strcmp(var, "ups.productid") == 0 && st.pid)
            nut_sendf(fd, "VAR %s %s \"%04x\"\n", ups, var, (unsigned)st.pid);
        else
            nut_sendf(fd, "ERR UNKNOWN-VAR\n");
        return;
    }

    if (strcasecmp(argv[0], "QUIT") == 0) {
        nut_sendf(fd, "OK Goodbye\n");
        sess->close_after = true;
        return;
    }

    nut_sendf(fd, "ERR INVALID-COMMAND\n");
}

static void nut_server_task(void *arg)
{
    app_cfg_t *cfg = (app_cfg_t *)arg;

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) { ESP_LOGE(TAG, "NUT socket() failed"); vTaskDelete(NULL); return; }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NUT_TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "NUT bind() failed"); close(s); vTaskDelete(NULL); return;
    }
    if (listen(s, 4) != 0) {
        ESP_LOGE(TAG, "NUT listen() failed"); close(s); vTaskDelete(NULL); return;
    }

    ESP_LOGI(TAG, "NUT server listening on tcp/%d (plaintext, recv_timeout=%ds, no-banner)",
             NUT_TCP_PORT, NUT_RECV_TIMEOUT_S);

    static int s_conn_count = 0;

    while (1) {
        struct sockaddr_in src4;
        socklen_t slen = sizeof(src4);
        int c = accept(s, (struct sockaddr *)&src4, &slen);
        if (c < 0) {
            ESP_LOGW(TAG, "accept() failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int conn_id = ++s_conn_count;
        char client_ip[16] = "?";
        inet_ntoa_r(src4.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "[#%d] connect from %s:%u",
                 conn_id, client_ip, (unsigned)ntohs(src4.sin_port));

        struct timeval tv  = { .tv_sec = NUT_RECV_TIMEOUT_S, .tv_usec = 0 };
        struct timeval stv = { .tv_sec = NUT_SEND_TIMEOUT_S, .tv_usec = 0 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv,  sizeof(tv));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

        nut_session_t sess = {0};
        char buf[512];
        int used = 0;

        while (1) {
            int r = recv(c, buf + used, (int)sizeof(buf) - 1 - used, 0);
            if (r <= 0) {
                if (r == 0) ESP_LOGI(TAG, "[#%d] client closed connection", conn_id);
                else        ESP_LOGW(TAG, "[#%d] recv error errno=%d (timeout or reset)", conn_id, errno);
                break;
            }
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

                if (sess.close_after) {
                    ESP_LOGI(TAG, "[#%d] closing after response (LIST VAR / QUIT)", conn_id);
                    goto done;
                }
            }

            int rem = (int)strlen(start);
            if (rem > 0 && start != buf) memmove(buf, start, rem + 1);
            used = rem;
            if (used >= (int)sizeof(buf) - 1) used = 0;
        }

done:
        shutdown(c, SHUT_RDWR);
        close(c);
        ESP_LOGI(TAG, "[#%d] disconnected", conn_id);
    }
}

void nut_server_start(app_cfg_t *cfg)
{
    xTaskCreate(nut_server_task, "nut_srv", 8192, cfg, 5, NULL);
}
