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
6. **Privilege gate** — probe root / `doas -n` / `sudo -n` before any mutation.  
   - **Has admin** → Configure / Create / Turn on|off / Delete enabled.  
   - **No admin** → **information-only mode**: tray + iface list + status only;  
     no editor save, no create/destroy, no up/down. Never crash; never pretend.  

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

```mermaid
flowchart TB
  UI["Panel tray + popover<br/>no ifconfig · no #ifdef"]
  ORCH["NetworkManager orchestrator<br/>signals only · OS-agnostic"]
  FACT["NetworkBackendFactory + Builder"]
  FB["FreeBSDNetworkBackend<br/>probe · fingerprint · primary · features<br/>apply Op → ifconfig/sysrc/wpa_cli"]
  LN["Linux path<br/>NetworkManager D-Bus only"]

  UI --> ORCH
  ORCH --> FACT
  FACT -->|freebsd| FB
  FACT -->|linux| LN
  FB -->|"never throw · fail-soft"| UI
  LN --> UI
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

```mermaid
flowchart TB
  subgraph tray [Panel tray]
    ICON["Icon only · activity colour"]
    TIP["Tooltip detail"]
  end

  subgraph orch [NetworkManager orchestrator]
    MAP["all_devices map"]
    PRI["primary_connection_obj"]
  end

  subgraph factory [Factory Method]
    F["NetworkBackendFactory::create"]
    F -->|freebsd| FB[FreeBSDNetworkBackend]
    F -->|linux| NMDB[NetworkManager D-Bus]
    F -->|other| NULL[Null backend]
  end

  subgraph sources [FreeBSD live sources — fail-soft]
    GIFA["getifaddrs · flags · IPv4/IPv6"]
    RT["route get default · v4/v6 gateway"]
    IFC["ifconfig detail · media · status"]
    WPA["wpa_cli later · scan/connect"]
    ADM["probe_admin_privilege · doas/sudo/root"]
  end

  ICON --> PRI
  PRI -->|signal_default_changed| ICON
  orch --> factory
  FB --> sources
  sources --> FB
  FB -->|InterfaceInfo + primary| MAP
  MAP --> PRI
  ADM -->|can_admin| UIGate[UI gate mutations]
```

### Privilege gate

```mermaid
flowchart LR
  A[Start mutation?] --> B{geteuid == 0?}
  B -->|yes| OK[AdminPrivilege Root]
  B -->|no| C{doas -n true?}
  C -->|yes| D[AdminPrivilege Doas]
  C -->|no| E{sudo -n true?}
  E -->|yes| F[AdminPrivilege Sudo]
  E -->|no| G[AdminPrivilege None]
  OK --> H[can_admin mutations]
  D --> H
  F --> H
  G --> I[information-only mode<br/>list + tooltip only]
```

### Autodetect features (same idea as audio `features()`)

| Flag | True when | UI |
|------|-----------|-----|
| `physical_ifaces` | any non-lo iface | interface list |
| `default_route` | `route get default` has interface | tray primary |
| `wireless` | any `wlanN` present | Wi‑Fi section |
| `wpa` | `wpa_cli` + control socket | scan/join controls |
| `can_admin` | root / `doas -n` / `sudo -n` | mutations; else **information-only** |
| `config_editor` | FreeBSD **and** `can_admin` | **Configure…** modal |
| `create_iface` | FreeBSD **and** `can_admin` | **Create…** + preflight |
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

### v3 — actions / policy

**Status:** design + mockup + pure helpers/tests. **Apply path (run ifconfig create/destroy/up/down) deferred.**

10. **Right-click** on interface row (detect live state)  
11. **Create…** cloned types  
12. **Preflight** before create  
13. Unit tests for pure parsers, preflight catalog, destroyable rules  

See **§3.1** for the full action design. Mockup: right-click + Create + knobs for gif-missing / no-admin.

---

## 3.1 Interface actions (right-click · Create · preflight)

### Context menu (per interface row)

```mermaid
flowchart TB
  RC[Right-click iface row] --> ADM{can_admin?}
  ADM -->|no| RO[Information-only<br/>items disabled · no toast pretend]
  ADM -->|yes| ST{InterfaceInfo.up<br/>IFF_UP live}
  ST -->|true| OFF[Menu: Turn off]
  ST -->|false| ON[Menu: Turn on]
  OFF --> CFG[Configure…]
  ON --> CFG
  CFG --> DES{is_destroyable_iface name?}
  DES -->|yes| DEL[Delete interface… enabled]
  DES -->|no| NOD[Delete disabled<br/>permanent NIC]
```

| Item | State detection | Planned apply | Gate |
|------|-----------------|---------------|------|
| **Turn off** | `InterfaceInfo.up == true` → label “Turn off” | `ifconfig IF down` | `can_admin` |
| **Turn on** | `InterfaceInfo.up == false` → label “Turn on” | `ifconfig IF up` | `can_admin` |
| **Configure…** | always (when FreeBSD + admin) | editor → sysrc | `config_editor` |
| **Delete interface…** | `is_destroyable_iface(name)` | `ifconfig IF destroy` | `can_admin` + destroyable |

Label comes from **live probe flags**, not a stale toggle. After apply (later), fingerprint refresh repaints the row (`admin down` badge when `!up`).

### Destroyable vs permanent

| May delete (`ifconfig IF destroy`) | Must **not** delete |
|------------------------------------|---------------------|
| `tapN`, `tunN`, `bridgeN`, `gifN`, `greN` | Physical NICs: `aq0`, `igb0`, `em0`, … |
| `vlanN`, `laggN`, `epairNa/b`, `vxlanN` | System loopback `lo0` |
| `loN` (N≥1), `wgN`, `stfN`, `vmnetN` | |
| `vm-*` (bhyve groups / ports) | |
| Cloned `wlanN` | Parent radio hardware (not listed as wlan unit alone if permanent — UI treats `wlanN` as destroyable clone) |

Pure helper: `wf_net::is_destroyable_iface(name)` — unit-tested; no I/O.

### Create… catalog

UI offers the **intersection** of:

1. Static known catalog (`known_clone_types()`): tap, tun, bridge, gif, gre, vlan, lagg, epair, vxlan, wg, lo, stf  
2. Live `ifconfig -C` (what this kernel currently registers)  
3. Preflight OK (module present / loadable / already in `-C`)

| Type | Typical module | Notes |
|------|----------------|-------|
| tap / tun | `if_tuntap` | VM / tunnel |
| bridge | `if_bridge` | |
| gif | `if_gif` | Often missing if module not installed |
| gre | `if_gre` | |
| vlan | `if_vlan` | |
| lagg | `if_lagg` | |
| epair | `if_epair` | Creates pair (a/b) |
| vxlan | `if_vxlan` | |
| wg | `if_wg` | WireGuard |
| lo | (kernel) | clone loopback |
| stf | `if_stf` | 6to4 |

Optional name field: empty → kernel assigns (`tap0`, `bridge1`, …).

### Preflight (before Create is enabled)

```mermaid
flowchart TB
  T[Select type e.g. gif] --> A{can_admin?}
  A -->|no| X1[Block: information-only]
  A -->|yes| C{type in ifconfig -C?}
  C -->|yes| OK[can_create · green strip]
  C -->|no| M{module known?}
  M -->|no module needed| OK2[can_create if catalog empty/test]
  M -->|yes| L{kldstat has module?}
  L -->|yes| OK
  L -->|no| F{.ko in /boot/kernel or /boot/modules?}
  F -->|yes| OK3[can_create · will load at apply]
  F -->|no| X2[Block: module not loaded and not found]
```

| Input | Source |
|-------|--------|
| clone catalog | `ifconfig -C` |
| module loaded | `kldstat` → `kldstat_has_module` |
| module file | `access(/boot/kernel/if_*.ko)` / `/boot/modules/` |
| admin | `probe_admin_privilege` |

Pure decision: `evaluate_create_preflight(...)`.  
Live: `probe_create_preflight(type)` / `probe_create_catalog()`.

**Never** enable Create when preflight fails. Show the reason string in the modal strip (green OK / red blocked). Mockup knob: “Simulate gif module missing”.

### Apply path (deferred — not in this milestone)

| Action | Command (host policy via doas/sudo) |
|--------|-------------------------------------|
| Up | `ifconfig IF up` |
| Down | `ifconfig IF down` |
| Destroy | `ifconfig IF destroy` |
| Create | `ifconfig TYPE create` [name] |

Elevation: reuse `AdminPrivilege` (root / `doas -n` / `sudo -n`). Host `doas.conf` is **not** in this repo. Fail-soft: toast/error string; never crash.

### Pure API surface (implemented)

| Symbol | Role |
|--------|------|
| `is_destroyable_iface` | Delete menu enable |
| `toggle_action_label` | Turn on / Turn off text |
| `known_clone_types` / `find_clone_type` | Create catalog |
| `parse_ifconfig_clone_list` | Parse `ifconfig -C` |
| `kldstat_has_module` | Module loaded? |
| `evaluate_create_preflight` | Pure gate |
| `probe_create_preflight` / `probe_create_catalog` | Live FreeBSD probe |

---

## 4. UI layout (popover — sparse)

```mermaid
flowchart TB
  subgraph pop [Network popover]
    H[Title: Network]
    I[Interfaces list<br/>per-row detail · no status strip under title]
    W[Wi-Fi section<br/>hidden if no wlan]
    F[Footer actions]
    H --> I --> W --> F
  end

  F -->|FreeBSD and can_admin| CFG[Configure…]
  F -->|FreeBSD and can_admin| CR[Create…]
  F -->|Linux only| ADV[Advanced → networkmgr]
  F -->|no can_admin| NONE[No mutation buttons]

  CFG --> M[Editor modal<br/>IPv4/IPv6 modes · static fields only]
  I -->|right-click · can_admin| CTX[Context menu<br/>Turn on/off · Configure · Delete]
  I -->|right-click · information-only| RO[No mutation items]
```

**Tray:** icon only (activity colour); tooltip + popover hold detail.

**Config (ini / wcm only):** show bridges/taps, poll interval, onclick command — not a toggle wall in the popover.

---

## 5. Mockup

| File | Purpose |
|------|---------|
| [mockup.html](mockup.html) | Click-through FreeBSD Network popover + editor/create/ctx |
| [diagrams/tray-icon-only.svg](diagrams/tray-icon-only.svg) | Tray: icon only, activity colour |
| [diagrams/popover-interfaces.svg](diagrams/popover-interfaces.svg) | FreeBSD popover (no NM / no status strip) |
| [diagrams/popover-context-menu.svg](diagrams/popover-context-menu.svg) | Right-click: up/down · configure · delete |
| [diagrams/modal-configure.svg](diagrams/modal-configure.svg) | Configure… static/DHCP editor |
| [diagrams/modal-create-preflight.svg](diagrams/modal-create-preflight.svg) | Create… + module preflight OK/blocked |
| [diagrams/information-only.svg](diagrams/information-only.svg) | No doas/sudo — read-only list |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Factory/Builder + Mermaid wiring |

Open locally:

```sh
xdg-open docs/network-control/mockup.html
# or
firefox docs/network-control/mockup.html
```

---

## 6. Acceptance

### v1 (status / Configure UI)

- [ ] Tray primary matches `route -n get default` interface  
- [ ] Popover lists real FreeBSD ifaces with IP/media  
- [ ] No NM-only chrome when backend is FreeBSD  
- [ ] Wi‑Fi section absent without wlan  
- [ ] Fingerprint skip on unchanged poll  
- [ ] FreeBSD: Configure… opens our editor modal (no networkmgr)  
- [ ] Linux: Advanced may open networkmgr  
- [ ] Editor writes/preview sysrc keys; elevate via doas/sudo  
- [x] gtest for classify / route parse / primary pick  

### v3 actions (design + pure helpers now; apply deferred)

- [x] Design: right-click Turn on/off from live `up` flag  
- [x] Design: Delete only when `is_destroyable_iface`  
- [x] Design: Create catalog + preflight (module / ifconfig -C)  
- [x] Mockup exercises context menu + Create + gif-missing / no-admin  
- [x] Pure gtest: destroyable rules, clone parse, preflight decision  
- [ ] UI wires context menu + Create modal to live probe  
- [ ] Apply: `ifconfig up|down|destroy|create` via admin (after sign-off)  

---

## 7. Collaboration

- Branch + commit + push OK; **no PR unless asked**  
- Author: Mark LaPointe \<mark@cloudbsd.org\>  
- Docs update with behaviour changes  
