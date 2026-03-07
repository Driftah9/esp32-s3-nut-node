# Changelog

All notable changes to esp32-s3-nut-node firmware.

---

## v14.25 — 2026-03-07

### Added
- **HTTP config portal** (tcp/80) with dashboard, config form, JSON status endpoint
- **HTTP Basic Auth** — portal protected by password (default: `upsmon`)
  - Warning shown on dashboard and config page until default password is changed
  - `/status` endpoint always open (unauthenticated) for scripts and monitoring
- **AJAX live polling** on dashboard — Status, Charge, and STA IP update every 5s
  without page reload; polling stops when tab is closed
- **SoftAP lifecycle** — AP disables after STA connects, re-enables after 60s if
  STA drops, disables again on reconnect
- `wifi_mgr_ap_is_active()` — query AP state from other modules
- `cfg_store_is_default_pass()` — detect unchanged factory password

### Fixed
- Stack overflow crash in `http_srv` task — rx and page buffers moved to heap
- Chrome `ERR_CONNECTION_RESET` on HTML pages — graceful socket close
  (`shutdown(WR)` + drain loop before `close()`)
- Blank page rendering — inline CSS (~1.8KB) was being silently truncated by
  intermediate `hdr[1024]` buffer; replaced with zero-CSS plain HTML

### Changed
- `portal_pass` defaults to `"upsmon"` on first boot (was empty/open)
- `HTTP_PAGE_BUF` set to 4096 (sufficient for plain HTML pages)
- Task stack reduced from 16384 to 6144 bytes (heap buffers removed stack pressure)

---

## v14.24 — 2026-03-07

### Added
- Model-aware HID decode for XS1500M
  - Report 0x0C: `battery.charge` (byte 1) and `battery.voltage` (bytes 2:3, ×1mV)
  - Report 0x21: `input.voltage` (byte 1, ×20000mV scale)
- Broad APC compatibility — generic fallback for unknown models
- `ups.mfr` and `ups.model` aliases (mirrors `device.mfr`/`device.model`)

### Changed
- `driver.version` bumped to 14.24

---

## v14.18 — 2026-03-07

### Fixed
- NUT server recv timeout (5s) — prevents hung connections blocking the server
- No-banner mode — removed NUT version banner for upsd compatibility

---

## v14.7 — 2026-03-07

### Changed
- Full modular refactor — split monolithic main.c into:
  `cfg_store`, `wifi_mgr`, `http_portal`, `nut_server`,
  `ups_state`, `ups_usb_hid`, `ups_hid_parser`

---

## v14.3 — 2026-03-07

### Added
- Stable baseline: NUT protocol, USB HID host, basic variable set confirmed
- Hot-plug stability (4 cycles confirmed, no crashes)
- `battery.voltage`, `input.voltage` confirmed on XS1500M
