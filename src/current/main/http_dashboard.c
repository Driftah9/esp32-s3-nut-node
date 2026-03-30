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
 R2  v15.14  NUT Variables lightbox: click link to see full upsc-style
             variable list fetched live from /status JSON. Groups:
             battery / input / ups / device / driver.
             Version strings bumped to 15.13.
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
        /* Lightbox overlay styles — appended after PORTAL_CSS */
        "<style>"
        "#nut-overlay{"
          "display:none;position:fixed;inset:0;"
          "background:rgba(0,0,0,0.6);"
          "z-index:100;align-items:center;justify-content:center;"
        "}"
        "#nut-overlay.open{display:flex;}"
        "#nut-box{"
          "background:#1e1e1e;border:1px solid #444;"
          "border-radius:8px;width:92%%;max-width:500px;"
          "max-height:80vh;display:flex;flex-direction:column;"
          "font-family:monospace;font-size:12px;"
        "}"
        "#nut-hdr{"
          "padding:10px 14px;border-bottom:1px solid #333;"
          "display:flex;justify-content:space-between;align-items:center;"
        "}"
        "#nut-hdr span{color:#ccc;font-size:11px;}"
        "#nut-hdr button{"
          "background:none;border:none;color:#888;"
          "font-size:18px;cursor:pointer;padding:0 4px;"
        "}"
        "#nut-body{overflow-y:auto;padding:6px 0;}"
        ".nut-grp{"
          "color:#666;font-size:10px;padding:6px 14px 3px;"
          "text-transform:uppercase;letter-spacing:0.05em;"
        "}"
        ".nut-row{"
          "display:flex;padding:2px 14px;"
          "border-bottom:1px solid #2a2a2a;"
        "}"
        ".nut-k{color:#888;min-width:210px;flex-shrink:0;}"
        ".nut-v{color:#e0e0e0;}"
        ".nut-v.ob{color:#f0a040;font-weight:bold;}"
        ".nut-v.ol{color:#4caf50;font-weight:bold;}"
        "#nut-ftr{"
          "padding:7px 14px;border-top:1px solid #333;"
          "color:#555;font-size:10px;"
        "}"
        "</style>"
        "</head><body>"
        "<h2>ESP32-S3 UPS Node</h2>"
        "<div class='subtitle'>v15.17 &mdash; NUT server on tcp/3493</div>"
        "%s"
        "<table id='ups_tbl'>"
        "<tr><th>Manufacturer</th><td>%s</td></tr>"
        "<tr><th>Model</th><td>%s</td></tr>"
        "<tr><th>Driver</th><td>esp32-nut-hid v15.17</td></tr>"
        "<tr><th>Status</th><td id='td_status' class='%s'>%s</td></tr>"
        "%s"
        "<tr><th>STA IP</th><td id='td_ip'>%s</td></tr>"
        "</table>"
        "<div class='poll' id='td_poll'></div>"
        "<div class='nav'>"
        "<a href='/config'>Configure</a>"
        "<a href='/status'>Status JSON</a>"
        "<a href='#' onclick='openNut();return false;'>NUT Variables</a>"
        "<a href='/compat'>Compatible UPS List</a>"
        "</div>"

        /* NUT lightbox overlay */
        "<div id='nut-overlay' onclick='if(event.target===this)closeNut();'>"
        "<div id='nut-box'>"
        "<div id='nut-hdr'>"
        "<div><b style='color:#ddd'>NUT variables</b>"
        "<span id='nut-cmd' style='margin-left:10px'></span></div>"
        "<button onclick='closeNut()'>&#x2715;</button>"
        "</div>"
        "<div id='nut-body'><div class='nut-grp'>Loading...</div></div>"
        "<div id='nut-ftr'>Live data &mdash; fetched from /status &middot; tcp/3493</div>"
        "</div></div>"

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

        /* NUT lightbox functions */
        "function row(k,v,cls){"
          "var s=cls?' class=\"nut-v '+cls+'\"':' class=\"nut-v\"';"
          "return '<div class=\"nut-row\"><span class=\"nut-k\">'+k+'</span><span'+s+'>'+v+'</span></div>';"
        "}"
        "function grp(label){return '<div class=\"nut-grp\">'+label+'</div>';}"
        "function n2(v,u){return v!==null&&v!==undefined?v+(u||''):'n/a';}"
        "function openNut(){"
          "document.getElementById('nut-overlay').classList.add('open');"
          "var x=new XMLHttpRequest();"
          "x.open('GET','/status',true);"
          "x.onload=function(){"
            "if(x.status!==200){document.getElementById('nut-body').innerHTML='<div class=\"nut-grp\">Error fetching /status</div>';return;}"
            "try{"
              "var d=JSON.parse(x.responseText);"
              "var ip=d.sta_ip||window.location.hostname;"
              "document.getElementById('nut-cmd').textContent='upsc '+d.ups_name+'@'+ip+':3493';"
              "var sc=d.ups_status||'UNKNOWN';"
              "var scls=sc.indexOf('OB')>=0?'ob':(sc.indexOf('OL')>=0?'ol':'');"
              "var rs=d.battery_runtime_s;"
              "var rtStr=rs!==null?(Math.floor(rs/60)+'m '+('0'+rs%%60).slice(-2)+'s  ('+rs+'s)'):'n/a';"
              "var rl=d.battery_runtime_low_s;"
              "var rtlStr=rl!==null?(Math.floor(rl/60)+'m ('+rl+'s)'):'n/a';"
              "var html='';"
              "html+=grp('battery');"
              "html+=row('battery.charge',n2(d.battery_charge,'%%'));"
              "html+=row('battery.charge.low',n2(d.battery_charge_low,'%%'));"
              "html+=row('battery.charge.warning',n2(d.battery_charge_warning,'%%'));"
              "html+=row('battery.runtime',rtStr);"
              "html+=row('battery.runtime.low',rtlStr);"
              "html+=row('battery.type','PbAc');"
              "if(d.battery_voltage_v!==null)html+=row('battery.voltage',d.battery_voltage_v.toFixed(3)+' V');"
              "if(d.battery_voltage_nominal_v!==null)html+=row('battery.voltage.nominal',d.battery_voltage_nominal_v+' V');"
              "html+=grp('input');"
              "html+=row('input.utility.present',d.ups_status&&d.ups_status.indexOf('OL')>=0?'1':'0');"
              "if(d.input_voltage_v!==null)html+=row('input.voltage',d.input_voltage_v.toFixed(1)+' V');"
              "if(d.input_voltage_nominal_v!==null)html+=row('input.voltage.nominal',d.input_voltage_nominal_v+' V');"
              "html+=grp('output');"
              "if(d.output_voltage_v!==null)html+=row('output.voltage',d.output_voltage_v.toFixed(1)+' V');"
              "html+=grp('ups');"
              "html+=row('ups.status',sc,scls);"
              "if(d.ups_load_pct!==null)html+=row('ups.load',d.ups_load_pct+'%%');"
              "html+=row('ups.type',n2(d.ups_type));"
              "html+=row('ups.firmware',n2(d.ups_firmware));"
              "html+=row('ups.test.result','No test initiated');"
              "html+=row('ups.delay.shutdown','20 s');"
              "html+=row('ups.delay.start','30 s');"
              "html+=row('ups.timer.reboot','-1');"
              "html+=row('ups.timer.shutdown','-1');"
              "html+=grp('device');"
              "html+=row('device.mfr',n2(d.device_mfr));"
              "html+=row('device.model',n2(d.device_model));"
              "html+=row('device.serial',n2(d.device_serial));"
              "html+=row('device.type','ups');"
              "html+=grp('driver');"
              "html+=row('driver.name','esp32-nut-hid');"
              "html+=row('driver.version',n2(d.driver_version));"
              "document.getElementById('nut-body').innerHTML=html;"
            "}catch(e){"
              "document.getElementById('nut-body').innerHTML='<div class=\"nut-grp\">Parse error</div>';"
            "}"
          "};"
          "x.onerror=function(){"
            "document.getElementById('nut-body').innerHTML='<div class=\"nut-grp\">Connection error</div>';"
          "};"
          "x.send();"
        "}"
        "function closeNut(){"
          "document.getElementById('nut-overlay').classList.remove('open');"
        "}"
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
