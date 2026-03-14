/*============================================================================
 MODULE: http_compat

 RESPONSIBILITY
 - Renders GET /compat — Compatible UPS device list page
 - Two-level expandable hierarchy: Vendor -> Series -> Model table
 - All data sourced from NUT usbhid-ups driver hardware compatibility list
 - Split from http_portal.c v15.10 to keep that file editable

 REVERT HISTORY
 R0  v15.10  Split from http_portal.c

 ADDING NEW DEVICES
 - Find the correct vendor block below
 - Add an MROW() entry inside the appropriate SOPEN/SCLOSE block
 - For a new vendor: add a VOPEN/VCLOSE block, add to ups_device_db.c
 - Bump the device count in the subtitle line in compat_head()
============================================================================*/

#include "http_compat.h"
#include "http_portal_css.h"   /* PORTAL_CSS shared macro */

#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Compat page CSS+JS header
 * ---------------------------------------------------------------------- */
static void compat_head(char *buf, size_t sz)
{
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

/* -------------------------------------------------------------------------
 * Helper macros — build expandable vendor/series/model rows
 * ---------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * render_compat — public entry point
 * ---------------------------------------------------------------------- */
void render_compat(char *out, size_t outsz)
{
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
    VOPEN(outsz, out, "CyberPower", "0764:xxxx",
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
        MROW(outsz, out, "5P (USB) / 5PX (USB)", "0463:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "5SC (USB) / 5SX (USB)", "0463:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "9E (USB) / 9PX (USB)", "0463:xxxx", "Standard HID", "NUT-listed.", ST_UN);
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
        MROW(outsz, out, "Nova AVR (USB)", "0463:xxxx", "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Pulsar EX/EXtreme/M/MX (USB)", "0463:xxxx",
             "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Comet EX RT (USB)", "0463:xxxx", "Standard HID (MGE)", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Powerware 5110/5115/5125/5130 (USB)", "0463:xxxx",
             "Standard HID", "NUT-listed. Powerware/Eaton branding.", ST_UN);
        MROW(outsz, out, "Powerware 9125/9130/9140/9155/9170/9355 (USB)", "0463:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Tripp Lite ════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Tripp Lite", "09AE:xxxx", "&#9711; Unconfirmed", COL_UN, "7 models");

      SOPEN(outsz, out, "SmartPro Series", "09AE:xxxx", "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "SMART500RT1U", "09AE:xxxx", "Standard HID + GET_REPORT",
             "NUT-listed. Feature report polling for values not on INT IN.", ST_UN);
        MROW(outsz, out, "SMART700USB", "09AE:xxxx", "Standard HID + GET_REPORT", "NUT-listed.", ST_UN);
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
    VOPEN(outsz, out, "Belkin", "050D:xxxx", "&#9711; Unconfirmed", COL_UN, "9 models");

      SOPEN(outsz, out, "F6H / F6C / Universal UPS Series", "050D:xxxx",
            "&#9711; Unconfirmed", COL_UN, "9 models");
        MROW(outsz, out, "F6H375-USB", "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Office Series F6C550-AVR", "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Regulator PRO-USB", "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Small Enterprise F6C1500-TW-RK", "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Universal UPS F6C100-UNV / F6C120-UNV", "050D:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Universal UPS F6C800-UNV / F6C1100-UNV / F6C1200-UNV",
             "050D:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ HP ════════════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "HP", "03F0:xxxx", "&#9711; Unconfirmed", COL_UN, "4 models");

      SOPEN(outsz, out, "T Series G2/G3", "03F0:xxxx", "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "T750 G2 (USB)",  "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T1000 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T1500 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "T3000 G3 (USB)", "03F0:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Dell ══════════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Dell", "047C:xxxx", "&#9711; Unconfirmed", COL_UN, "4 models");

      SOPEN(outsz, out, "H Series", "047C:xxxx", "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "H750E (USB)",  "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H950E (USB)",  "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H1000E (USB)", "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "H1750E (USB)", "047C:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Liebert / Vertiv ══════════════════════════════════════════════ */
    VOPEN(outsz, out, "Liebert / Vertiv", "10AF:xxxx", "&#9711; Unconfirmed", COL_UN, "2 models");

      SOPEN(outsz, out, "GXT / PSI Series", "10AF:xxxx", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "GXT4 (USB)", "10AF:xxxx", "Standard HID", "NUT-listed. Liebert subdriver.", ST_UN);
        MROW(outsz, out, "PSI5 (USB)", "10AF:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Powercom ══════════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Powercom", "0D9F:xxxx", "&#9711; Unconfirmed", COL_UN, "7 models");

      SOPEN(outsz, out, "All Series", "0D9F:xxxx", "&#9711; Unconfirmed", COL_UN, "7 models");
        MROW(outsz, out, "Black Knight Pro (USB)", "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Dragon (USB)",           "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Imperial (USB)",          "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "King Pro (USB)",          "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Raptor (USB)",            "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Smart King / Smart King Pro (USB)", "0D9F:xxxx",
             "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "WOW (USB)", "0D9F:xxxx", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);
    VCLOSE(outsz, out);

    /* ═══ Other Vendors ═════════════════════════════════════════════════ */
    VOPEN(outsz, out, "Other Vendors", "various",
          "&#9711; Unconfirmed", COL_UN, "19 manufacturers / 25+ models");

      SOPEN(outsz, out, "AEG Power Solutions", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "PROTECT NAS (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "PROTECT B (USB)",   "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Cyber Energy (ST Micro OEM)", "0483:A430",
            "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "Models with USB", "0483:A430", "Standard HID",
             "NUT-listed. OEM CyberPower variant using ST Micro VID.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Delta", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "Amplon RT Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Amplon N Series (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Dynex", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "DX-800U (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Ecoflow", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "Delta 3 Plus (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "EVER", "unknown", "&#9711; Unconfirmed", COL_UN, "3 models");
        MROW(outsz, out, "Sinline RT Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Sinline XL Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "ECO Pro Series (USB)",    "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Geek Squad", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "GS1285U (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "GoldenMate", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "UPS 1000VA Pro (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "IBM", "unknown", "&#9711; Unconfirmed", COL_UN, "various");
        MROW(outsz, out, "Various (USB port)", "unknown", "Standard HID",
             "NUT-listed generically. USB HID interface.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "iDowell", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "iBox UPS (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Ippon", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "Back Power Pro (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "Smart Power Pro (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Legrand", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "KEOR SP (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "MasterPower", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "MF-UPS650VA (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Minibox", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
        MROW(outsz, out, "openUPS Intelligent UPS (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "PowerWalker", "unknown", "&#9711; Unconfirmed", COL_UN, "4 models");
        MROW(outsz, out, "VI 650 SE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 850 SE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 1000 SE (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "VI 1500 SE (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Powervar", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "ABCE (USB)",  "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "ABCEG (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Rocketfish", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "RF-1000VA (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "RF-1025VA (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Salicru", "unknown", "&#9711; Unconfirmed", COL_UN, "2 models");
        MROW(outsz, out, "SPS One Series (USB)",    "unknown", "Standard HID", "NUT-listed.", ST_UN);
        MROW(outsz, out, "SPS Xtreme Series (USB)", "unknown", "Standard HID", "NUT-listed.", ST_UN);
      SCLOSE(outsz, out);

      SOPEN(outsz, out, "Syndome", "unknown", "&#9711; Unconfirmed", COL_UN, "1 model");
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
