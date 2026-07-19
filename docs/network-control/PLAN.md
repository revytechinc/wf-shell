# Panel Network Control — Plan (FreeBSD-first)

**Maintainer:** REVYTECH, Inc.  
**Repo:** [github.com/revytechinc/wf-shell](https://github.com/revytechinc/wf-shell)  
**Branch:** `feature/panel-network`  
**Status:** Wiring design + mockup; FreeBSD probe/Factory started  
**Runtime target:** FreeBSD-centric — **we own the FreeBSD network control plane in-process**  
**Also:** Linux keeps **real** NetworkManager D-Bus; FreeBSD does **not** shim NM.

---

## 0. Decision: FreeBSD network manager (in-tree)

We **will implement our own FreeBSD network manager** as a library inside `wf-shell`
(same process as the panel), not as a NetworkManager clone or a D-Bus compatibility layer.

| | FreeBSD (ours) | Linux (upstream path) |
|--|----------------|------------------------|
| **Product name (concept)** | `FreeBSDNetworkService` / `INetworkBackend` FreeBSD product | NetworkManager via D-Bus |
| **Where it lives** | `src/util/network/` (Factory product) | existing `manager.cpp` NM path |
| **Sources of truth** | kernel + base tools | NM daemon |
| **UI** | panel tray + popover (mockup) | existing NM widgets |
| **Privilege** | best-effort unprivileged read; actions may need root/doas | NM polkit |

### What “our own manager” means (and does **not**)

**Is:**

1. **Read path (v1)** — continuous truth from FreeBSD:
   - `getifaddrs` / iface groups / media / status  
   - default route + gateway  
   - fingerprint → signals (no spam)  
   - primary selection for tray  
2. **Policy path** — which ifaces to show (hide lo, optional bridge/tap via Builder/ini)  
3. **Action path (v2+)** — small, explicit ops when allowed:
   - `ifconfig <if> up|down`  
   - `wpa_cli` scan / select / disconnect (if `wlan*` + wpa)  
4. **Config editor (FreeBSD)** — modal UI writing `sysrc` / `rc.conf` keys  
   (`ifconfig_IF`, `ifconfig_IF_ipv6`, optional gateways) via `doas`/`sudo`  
   (host policy, not shipped in the repo).  
   - **DHCP** → no manual `defaultrouter` (lease provides gateway).  
   - **SLAAC / accept_rtadv** → no manual `ipv6_defaultrouter` (RA provides routes).  
   - **Static** → address/prefix + optional gateway fields.  
5. **Feature autodetection** — same spirit as audio `features()`  

**Linux-only:** NetworkManager D-Bus + optional **Advanced** → `networkmgr`.  
**FreeBSD:** no NetworkManager chrome; no networkmgr Advanced button.

**Is not:**

- A system-wide daemon replacing `wpa_supplicant` / `dhclient` / `netif`  
- NetworkManager D-Bus API compatibility on FreeBSD  
- Full routing / firewall / jail networking IDE  
- Reimplementing DHCP in the panel  

Privileged work stays with **base tools** (`ifconfig`, `sysrc`, `wpa_cli`, `dhclient`). We orchestrate and present.

### Why

- FreeBSD has **no** NetworkManager as the normal desktop path.  
- Fake NM object paths and GNOME control-center were a dead end.  
- Factory/Builder already isolates FreeBSD; the “manager” is that product grown up.  
- Sound Settings proved the pattern: **OS-native stack + sparse UI + fail-soft**.

### Layering (target)

```text
  Panel (tray + popover)          ← no ifconfig, no #ifdef
           │
  NetworkManager (signals only)   ← thin orchestrator, OS-agnostic
           │
  INetworkBackend (Factory)
           │
  ┌────────┴────────┐
  FreeBSDNetworkService     Linux → NM D-Bus
  · probe / fingerprint
  · primary / gateway
  · features()
  · apply(Op) → ifconfig/wpa_cli
  · never throw
```

Name in code can stay `FreeBSDNetworkBackend` until it grows; docs call it the
**FreeBSD network service** so we do not confuse it with GNOME NetworkManager.

---

## 1. Problem

Tray **network** next to sound is NM-shaped:

- Linux: NetworkManager + Wi‑Fi AP list works  
- FreeBSD: thin `getifaddrs` poll; tray often wrong primary; popover has no FreeBSD story  
- Right-click default was GNOME control center (useless here)  
- `is_active()` was non-virtual → crash risk on FreeBSD devices  

We already started: **Factory/Builder**, `wf_net::probe_interfaces()`, primary from **default route**, richer display names. Icons for bridge/tap fixed to theme-available names.

---

## 2. FreeBSD wiring (how data moves)

```text
┌─────────────────────────────────────────────────────────────┐
│  Panel tray  (WayfireNetworkInfo)                           │
│  icon + short label ← Connection(primary device)            │
└───────────────────────────▲─────────────────────────────────┘
                            │ signal_default_changed
┌───────────────────────────┴─────────────────────────────────┐
│  NetworkManager (orchestrator, OS-agnostic signals)         │
│  all_devices map · primary_connection_obj                   │
└───────────────────────────▲─────────────────────────────────┘
                            │ Factory product
┌───────────────────────────┴─────────────────────────────────┐
│  NetworkBackendFactory::create()                            │
│   freebsd → FreeBSDNetworkBackend (poll)                    │
│   linux   → D-Bus NM (manager path)                         │
│   other   → Null                                            │
└───────────────────────────▲─────────────────────────────────┘
                            │ probe_interfaces(ProbeOptions)
┌───────────────────────────┴─────────────────────────────────┐
│  FreeBSD live sources (fail-soft, never throw)              │
│  · getifaddrs(3)     flags, IPv4/IPv6                       │
│  · route -n get default    primary iface + gateway          │
│  · ifconfig <iface>  media, status, ether                   │
│  · (later) wpa_cli   scan / status / connect if wlan*       │
│  · (later) networkmgr or custom onclick for advanced UI     │
└─────────────────────────────────────────────────────────────┘
```

### Autodetect features (same idea as audio `features()`)

| Flag | True when | UI |
|------|-----------|-----|
| `physical_ifaces` | any non-lo iface | interface list |
| `default_route` | `route get default` has interface | status strip + tray primary |
| `wireless` | any `wlanN` present | Wi‑Fi section |
| `wpa` | `wpa_cli` + control socket | scan/join controls |
| `config_editor` | FreeBSD always (our modal) | **Configure…** → editor modal |
| `advanced_gui` | Linux + `networkmgr` / ini | **Advanced** (hidden on FreeBSD) |

**Only show sections that probe true.** No fake Wi‑Fi on a wired-only box.

### Fingerprint / no rebuild if unchanged

| Object | Fingerprint |
|--------|-------------|
| Interface row | name + up/running + addrs + media + default |
| Primary | default-route iface path |
| Wi‑Fi scan list (later) | BSSID+SSID+signal set |

Same Honcho rule as audio: **diff before paint**.

---

## 3. Goals — “all the things” (phased)

### v1 — wired / multi-NIC status (this mockup)

1. Tray: correct **primary** (default route), icon, short label (iface or IP).  
2. Popover **Network**: status strip (gateway, primary, link).  
3. List **interfaces** FreeBSD knows about (eth, bridge, tap — filter via Builder).  
4. Per-iface: kind, IPs, media/status, **default** badge.  
5. FreeBSD **Configure…** → modal editor (rc.conf / sysrc); **not** networkmgr.  
6. Linux **Advanced** → networkmgr / ini only.  
7. No NM global toggles / fake AP list on FreeBSD.

### v2 — Wi‑Fi (when `wlan*` + wpa)

7. Scan list, connect/disconnect via `wpa_cli`.  
8. Signal strength icons.  
9. PSK prompt (reuse password popup pattern carefully).

### v3 — actions / policy (**planned — not implemented yet**)

10. **Right-click** on interface row (detect live state):  
    - Turn on / Turn off (`ifconfig IF up|down`)  
    - Configure… → editor modal  
    - Delete interface… when destroyable (`ifconfig IF destroy`) — tap, bridge, gif, vlan, lagg, epair, lo clones; **not** permanent NICs (aq0, igb0, …)  
11. **Create…** cloned types from `ifconfig -C` catalog (tap, tun, bridge, gif, vlan, lagg, epair, …)  
12. **Preflight** before create: kernel module present/loadable (`kldstat` / `if_*.ko`); disable Create with reason if e.g. `if_gif` missing  
13. Unit tests for pure parsers, preflight catalog, destroyable rules  

Mockup exercises these flows; C++ apply path deferred until planning sign-off.

---

## 4. UI layout (popover — sparse)

```text
┌─ Network ───────────────────────────── ✕ ┐
│  INTERFACES                              │  ← no status strip under title
│  ● aq0  Ethernet · default               │     (detail is per-row)
│    99.48…                                │
│    2600:…                                │
│  ○ igb0 …                                │
├──────────────────────────────────────────┤
│  WI‑FI          (hidden if no wlan)      │
│  … SSID · 5 GHz · Wi-Fi 6 …              │
├──────────────────────────────────────────┤
│  [ Configure… ]   ← FreeBSD editor modal │
│  (Linux only: Advanced → networkmgr)     │
└──────────────────────────────────────────┘

┌─ Configure aq0 ──────────────────────────┐  ← modal
│  IPv4 mode · address/prefix · gateway    │
│  IPv6 mode · address/prefix · gateway    │
│  Save → doas sysrc …                     │
└──────────────────────────────────────────┘
```

**Tray:** icon only (activity colour); tooltip + popover hold detail.

**Config (ini / wcm only):** show bridges/taps, poll interval, onclick command — not a toggle wall in the popover.

---

## 5. Mockup

| File | Purpose |
|------|---------|
| [mockup.html](mockup.html) | Click-through FreeBSD Network popover |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Factory/Builder + probe diagram |

Open locally:

```sh
xdg-open docs/network-control/mockup.html
# or
firefox docs/network-control/mockup.html
```

---

## 6. Acceptance (v1)

- [ ] Tray primary matches `route -n get default` interface  
- [ ] Popover lists real FreeBSD ifaces with IP/media  
- [ ] No NM-only chrome when backend is FreeBSD  
- [ ] Wi‑Fi section absent without wlan  
- [ ] Fingerprint skip on unchanged poll  
- [ ] FreeBSD: Configure… opens our editor modal (no networkmgr)  
- [ ] Linux: Advanced may open networkmgr  
- [ ] Editor writes/preview sysrc keys; elevate via doas/sudo  
- [ ] gtest for classify / route parse / primary pick  

---

## 7. Collaboration

- Branch + commit + push OK; **no PR unless asked**  
- Author: Mark LaPointe \<mark@cloudbsd.org\>  
- Docs update with behaviour changes  
