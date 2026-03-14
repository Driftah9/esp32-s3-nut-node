/*============================================================================
 MODULE: http_portal_css

 RESPONSIBILITY
 - Shared PORTAL_CSS macro used by http_portal.c and http_compat.c
 - Dark industrial theme, no external resources
 - Include this header in any file that renders a full HTML page

 REVERT HISTORY
 R0  v15.10  Split from http_portal.c
============================================================================*/

#pragma once

/* Shared CSS injected into every page — dark industrial theme */
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
    ".poll{color:#555;font-size:0.75em;font-family:Arial,sans-serif;margin-top:8px}" \
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
