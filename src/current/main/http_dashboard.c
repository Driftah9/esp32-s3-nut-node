/*============================================================================
 MODULE: http_dashboard

 RESPONSIBILITY
 - Renders GET / -- UPS status dashboard with AJAX live polling
 - Static table on first load; AJAX polls /status every 5s

 REVERT HISTORY
 R0  v15.11  Split from http_portal.c
 R1  v15.11  Fix AJAX addOrUpdate ID mismatch (tr_charge->charge etc)
             Fix rid=0x21 runtime source for CyberPower
             Add uid=0x0085/0x008B/0x002C to parser cache (Smart-UPS C)
============================================================================*/

#include "http_dashboard.h"
#include "http_portal_css.h"
#include "ups_state.h"
#include "wifi_mgr.h"
#include "cfg_store.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static void strlcpy0(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strlcpy(dst, src, dstsz);
}

void render_dashboard(app_cfg_t *cfg, char *out, size_t outsz)
{
    ups_state_t ups;
    ups_state_snapshot(&ups);

    const char *st = ups.ups_status[0] ? ups.ups_status : "UNKNOWN";
    char s_mfr[80], s_model[80], sta_ip[16];
    strlcpy0(s_mfr,   ups.manufacturer[0] ? ups.manufacturer : "-", sizeof(s_mfr));
    strlcpy0(s_model, ups.product[0]      ? ups.product      : "-", sizeof(s_model));
    wifi_mgr_sta_ip_str(sta_ip);

    /* Build optional rows for initial static render */
    char opt_rows[512] = {0};

    if (ups.battery_charge > 0 || ups.valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='charge'><th>Charge</th><td id='td_charge'>%u%%</td></tr>",
            ups.battery_charge);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.battery_runtime_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='runtime'><th>Runtime</th><td id='td_runtime'>%lum %02lus</td></tr>",
            (unsigned long)ups.battery_runtime_s / 60,
            (unsigned long)ups.battery_runtime_s % 60);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.battery_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='bvolt'><th>Batt Voltage</th><td id='td_bvolt'>%.3f V</td></tr>",
            ups.battery_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.ups_load_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='load'><th>Load</th><td id='td_load'>%u%%</td></tr>",
            ups.ups_load_pct);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.input_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='ivolt'><th>Input Voltage</th><td id='td_ivolt'>%.1f V</td></tr>",
            ups.input_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }
    if (ups.output_voltage_valid) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
            "<tr id='ovolt'><th>Output Voltage</th><td id='td_ovolt'>%.1f V</td></tr>",
            ups.output_voltage_mv / 1000.0f);
        strlcat(opt_rows, tmp, sizeof(opt_rows));
    }

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
        "<div class='subtitle'>v15.11 &mdash; NUT server on tcp/3493</div>"
        "%s"
        "<table id='ups_tbl'>"
        "<tr><th>Manufacturer</th><td>%s</td></tr>"
        "<tr><th>Model</th><td>%s</td></tr>"
        "<tr><th>Driver</th><td>esp32-nut-hid v15.11</td></tr>"
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
            "tr=document.createElement('tr');tr.id=id;"
            "var ht=document.createElement('th');ht.textContent=lbl;"
            "var dt=document.createElement('td');dt.id='td_'+id;"
            "tr.appendChild(ht);tr.appendChild(dt);"
            "var ip=document.querySelector('#ups_tbl tr:last-child');"
            "ip.parentNode.insertBefore(tr,ip);"
          "}"
          "var td=document.getElementById('td_'+id);"
          "if(td)td.textContent=val;"
        "}"
        "var lastOk=null;"
        "function fmtTime(d){"
          "var h=d.getHours(),m=d.getMinutes(),s=d.getSeconds();"
          "var ap=h>=12?'PM':'AM';h=h%%12||12;"
          "return h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s+' '+ap;"
        "}"
        "var ck=setInterval(function(){"
          "var el=document.getElementById('td_poll');"
          "var now=fmtTime(new Date());"
          "el.textContent=lastOk?('Now: '+now+' | Last poll: '+fmtTime(lastOk)):('Now: '+now+' | Polling...');"
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
                "if(d.battery_charge!==null)addOrUpdate('charge','Charge',d.battery_charge+'%%');"
                "if(d.battery_runtime_s!==null){"
                  "var rs=d.battery_runtime_s;"
                  "var rm=Math.floor(rs/60);var rse=rs%%60;"
                  "var rt=rm>0?(rm+'m '+(rse<10?'0':'')+rse+'s'):rse+'s';"
                  "addOrUpdate('runtime','Runtime',rt);"
                "}"
                "if(d.battery_voltage_v!==null)addOrUpdate('bvolt','Batt Voltage',d.battery_voltage_v.toFixed(3)+' V');"
                "if(d.input_voltage_v!==null)addOrUpdate('ivolt','Input Voltage',d.input_voltage_v.toFixed(1)+' V');"
                "if(d.output_voltage_v!==null)addOrUpdate('ovolt','Output Voltage',d.output_voltage_v.toFixed(1)+' V');"
                "if(d.ups_load_pct!==null)addOrUpdate('load','Load',d.ups_load_pct+'%%');"
                "lastOk=new Date();"
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
