# M9 — NUT Hub Setup on linuxtest LXC
# ESP32-S3 NUT Node project
# Updated: 2026-03-07

## Overview

linuxtest LXC (Proxmox) acts as the NUT netserver hub.
All other machines (HA, upsmon clients) point at linuxtest, not directly at the ESP32 nodes.
The ESP32 node speaks the NUT protocol natively on tcp/3493 — linuxtest proxies it via the
`nutclient` driver (no USB, no local HID — pure network proxy).

```
[XS1500M UPS]
     |USB
[ESP32-S3 NUT Node]  tcp/3493  -->  [linuxtest LXC]  tcp/3493  -->  [Home Assistant]
     10.0.0.190                          <static IP>                  [Proxmox upsmon]
                                                                       [Windows upsmon]
```

---

## Pre-requisites

### On Proxmox — before starting
1. Set a static IP on the linuxtest LXC:
   - Proxmox UI → linuxtest → Network → Edit eth0
   - Set static IP (e.g. 10.0.0.50/16, gw 10.0.0.1, dns 10.0.0.1)
   - Or edit inside the LXC: `/etc/network/interfaces`

2. Note the chosen static IP — replace `<HUB_IP>` throughout this doc.

### On linuxtest LXC
```bash
apt update
apt install -y nut nut-client
nut --version   # confirm 2.8.x
```

---

## Step 1 — nut.conf

```bash
nano /etc/nut/nut.conf
```

```ini
MODE=netserver
```

---

## Step 2 — ups.conf

Each ESP32 node gets one entry. The `nutclient` driver proxies NUT-over-TCP.

```bash
nano /etc/nut/ups.conf
```

```ini
[xs1500m]
    driver   = nutclient
    port     = 10.0.0.190:3493
    desc     = "Back-UPS XS 1500M (ESP32 node)"
    login    = admin
    password = admin
```

> **Note:** `login`/`password` match the NUT credentials set in the ESP32
> portal at `/config` → NUT Username / NUT Password (defaults: admin/admin).
> If you changed them on the ESP32, update here to match.

---

## Step 3 — upsd.conf

Tells upsd which interface to bind and who can connect.

```bash
nano /etc/nut/upsd.conf
```

```ini
LISTEN 0.0.0.0 3493
# Or bind to specific LAN IP only:
# LISTEN <HUB_IP> 3493
```

---

## Step 4 — upsd.users

Defines accounts that NUT clients (HA, upsmon) use to authenticate to THIS hub.

```bash
nano /etc/nut/upsd.users
```

```ini
[upsmon]
    password  = upsmon
    upsmon    = master

[admin]
    password  = admin
    actions   = set
    instcmds  = all
```

> The `upsmon` account is what HA and other upsmon clients will use.
> Change the password to something stronger in production.

---

## Step 5 — upsmon.conf (optional — only if linuxtest itself shuts down)

Only needed if linuxtest runs upsmon locally to trigger Proxmox shutdown.
Skip this step if linuxtest is just a proxy.

```bash
nano /etc/nut/upsmon.conf
```

```ini
MONITOR xs1500m@localhost 1 upsmon upsmon master

MINSUPPLIES 1
SHUTDOWNCMD "/sbin/shutdown -h now"
NOTIFYCMD /usr/sbin/upssched
POLLFREQ 5
POLLFREQALERT 5
HOSTSYNC 15
DEADTIME 15
POWERDOWNFLAG /etc/killpower
RBWARNTIME 43200
NOCOMMWARNTIME 300
FINALDELAY 5
```

---

## Step 6 — Start and enable services

```bash
systemctl enable nut-server
systemctl enable nut-client
systemctl restart nut-server
systemctl restart nut-client
systemctl status nut-server
systemctl status nut-client
```

---

## Step 7 — Verify

### From linuxtest itself:
```bash
upsc xs1500m@localhost
```

Expected output — same 17 variables as direct query to ESP32:
```
battery.charge: 97
battery.charge.low: 20
battery.type: PbAc
battery.voltage: 34.920
input.utility.present: 1
input.voltage: 120.0
ups.status: OL
...
```

### From another machine (e.g. your Windows box or HA):
```bash
upsc xs1500m@<HUB_IP>
```

### Quick connectivity test before NUT is set up:
```bash
nc -zv <HUB_IP> 3493   # should say "open"
```

---

## Step 8 — Firewall

If ufw is active on linuxtest:
```bash
ufw allow from 10.0.0.0/16 to any port 3493
```

---

## Troubleshooting

### upsd won't start — "nutclient driver not found"
```bash
apt install -y nut-driver-enumerator
```

### "Data stale" or connection refused from ESP32
- Check ESP32 is reachable: `nc -zv 10.0.0.190 3493`
- Check NUT user/pass in ups.conf matches ESP32 portal settings
- Check ESP32 portal `/status` JSON: `ups_valid` should be `true`

### upsc returns empty or partial data
- The ESP32 closes the connection after LIST VAR (single-client design)
- This is normal for v14.25 — nutclient driver handles reconnect per-poll

### Logs
```bash
journalctl -u nut-server -f
journalctl -u nut-client -f
tail -f /var/log/syslog | grep -i nut
```

---

## Home Assistant NUT integration

Once hub is verified:
Settings → Integrations → Add → Network UPS Tool (NUT)
- Host: `<HUB_IP>`
- Port: `3493`
- Username: `upsmon`
- Password: `upsmon`

Select the `xs1500m` UPS when prompted.

---

## Adding a second ESP32 node later

Add another `[ups_name]` block in `/etc/nut/ups.conf`:

```ini
[br1000g]
    driver   = nutclient
    port     = <second-esp32-ip>:3493
    desc     = "Back-UPS BR 1000G (ESP32 node)"
    login    = admin
    password = admin
```

Then `systemctl restart nut-server`.
No changes needed in HA — it will discover the new UPS automatically.
