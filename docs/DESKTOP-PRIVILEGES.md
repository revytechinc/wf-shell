# Desktop privileges (FreeBSD) — Settings without sudo prompts

## Goal

A normal desktop session user should change **personal and seat** settings
without typing a password. Only true **system** mutations need elevation, and
those should use **narrow** rules (not “full root via doas”).

## What never needs root

These write only user or seat state:

| Area | Path / mechanism |
|------|------------------|
| Panel / theme / dock | `~/.config/wf-shell.ini`, `~/.config/wf-shell/config.json` |
| Wayfire keybinds / layout | `~/.config/wayfire.ini` |
| Display modeset (live) | Wayland compositor + **seatd** + **video** group |
| Session env | user process |

**Requirement:** user in group `video`, `seatd` running, DRM nodes usable.

```sh
pw groupmod video -m "$USER"    # once, as root
sysrc seatd_enable=YES
service seatd start
```

## What traditionally needed root on FreeBSD

| Area | Binary / resource | Clean unprivileged path |
|------|-------------------|-------------------------|
| Shutdown / reboot | `/sbin/shutdown` **setuid**, group **operator** | Put desktop users in **operator** |
| Suspend | `zzz` / `acpiconf -s 3` | Often needs privilege; use **operator** or narrow **doas** for those cmds only |
| Wi‑Fi scan/join | `wpa_cli` vs `/var/run/wpa_supplicant/*` | Socket group (often **wheel**) — already works if user in wheel |
| Interface create / ifconfig | `ifconfig` | Narrow **doas** `cmd ifconfig` or keep elevated helper |
| System defaults under `/usr/local/etc` | package files | **Never** write from Settings — only seed **to** `~/.config` |

## Recommended FreeBSD layout

### 1. Groups

```sh
pw groupmod video    -m desktopuser
pw groupmod operator -m desktopuser   # setuid shutdown/reboot
# wheel only if you want admin / wpa control socket access
```

### 2. Prefer operator over open doas for power

`/sbin/shutdown` is mode `4550` root:operator. Membership in **operator**
lets the desktop run shutdown/reboot **without** doas.

### 3. Narrow doas (not the sample file)

The stock sample `permit nopass :wheel` is a **full passwordless root shell**.
Replace with explicit commands only, e.g.:

```
# /usr/local/etc/doas.conf — REVYTECH desktop (example)
permit persist :wheel

# Network (panel Wi‑Fi / ifconfig)
permit nopass :wheel cmd ifconfig
permit nopass :wheel cmd wpa_cli
permit nopass :wheel cmd wpa_supplicant
permit nopass :wheel cmd sysrc

# Power if not using operator for suspend
permit nopass :wheel cmd zzz
permit nopass :wheel cmd acpiconf
permit nopass :wheel cmd /sbin/shutdown
```

`persist` keeps a short password cache for rare admin tasks; day-to-day
Settings paths should not need it.

### 4. What wf-settings / panel should do

1. **User configs** — create under `~/.config` (no elevation).  
2. **Display** — modeset via compositor APIs / tools as the seat user.  
3. **Power** — run `Capability.command` only if `permitted`; FreeBSD backend
   should treat **operator** as permitted for shutdown/reboot.  
4. **Network** — try unprivileged `wpa_cli`; elevate **only** via
   `doas -n` / `sudo -n` for ifconfig (passwordless rules above).  
5. **Never** open a root file dialog into `/usr/local/etc` for “user prefs.”

## Quick audit commands

```sh
id
groups
ls -l /sbin/shutdown /var/run/seatd.sock /var/run/wpa_supplicant
doas -n true && echo 'passwordless doas works' || echo 'doas needs password or denied'
/sbin/shutdown -h +999 2>&1 | head -1   # expect usage if operator; permission denied if not
```

## Enable helper (VALIDATE then APPLY)

On **every** `revytech-wayfire` package install, `pkg-install` runs:

```sh
revytech-desktop-privileges-enable [installing-user]
```

That script **validates** each requirement, prints `PASS` / `NEED` / `FIXED` /
`FAIL`, and **only adds** what is missing (groups, seatd, doas rules, default
sanity). Safe to re-run.

```sh
# Full validate + apply (as root)
doas revytech-desktop-privileges-enable
doas revytech-desktop-privileges-enable mlapointe

# Validate only (exit 1 if anything still wrong)
doas revytech-desktop-privileges-enable --check
```

Installed as `/usr/local/sbin/revytech-desktop-privileges-enable`.

## Regression ideas

- User can write `~/.config/wf-shell.ini` without elevation.  
- User in `video` + seatd → compositor starts.  
- User in `operator` → `/sbin/shutdown` executable (permission check).  
- Network elevation uses `doas -n` only (no interactive password in GUI path).
