/*============================================================================
 MODULE: http_config_page

 RESPONSIBILITY
 - Renders GET /config -- configuration form
 - Parses POST /save form body (URL-encoded key=value pairs)
 - Split from http_portal.c v15.11

 REVERT HISTORY
 R0  v15.11  Split from http_portal.c
============================================================================*/

#include "http_config_page.h"
#include "http_portal_css.h"
#include "wifi_mgr.h"
#include "cfg_store.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#define HTTP_BODY_MAX 2048

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
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

void render_config(app_cfg_t *cfg, char *out, size_t outsz,
                   const char *note, const char *note_cls)
{
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
        "<div class='subtitle'>v15.11 &mdash; Configuration</div>"
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

void parse_form_kv(app_cfg_t *cfg_inout, const char *body,
                   char *action_out, size_t action_sz)
{
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
