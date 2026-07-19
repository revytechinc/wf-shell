# Network backend architecture (Factory + Builder)

**Maintainer:** REVYTECH, Inc. · **Repo:** [revytechinc/wf-shell](https://github.com/revytechinc/wf-shell)  

**Orientation:** FreeBSD-first live probe. **NetworkManager is Linux-only.**  
Missing modules → hide panels — **never crash**.

---

## Patterns

| Pattern | Where |
|---------|--------|
| Factory Method | `NetworkBackendFactory::create()` / `Builder::build()` |
| Builder | `poll_interval_ms`, `include_virtual`, `include_bridge`, … |
| Abstract product | `NetworkBackend` (FreeBSD poll product vs null on Linux) |
| Domain types | `wf_net::InterfaceInfo`, `InterfaceKind`, `ProbeOptions` |
| Fail-soft | empty probe / no default route → null primary, quiet tray |
| Fingerprint | `interface_fingerprint()` before emit/rebuild |
| UI split | `NetworkControlWidget`: `setup_freebsd_ui()` vs `setup_linux_ui()` |

### Product matrix

| OS | Factory product | Orchestrator path | Popover UI |
|----|-----------------|-------------------|------------|
| **FreeBSD** | `FreeBSDNetworkBackend` | probe + primary signals only | iface list only — **no NM** |
| **Linux** | null poll backend | NetworkManager D-Bus | NM toggles, VPN, modem, “NM not running” |
| Other | null | quiet | empty |

---

## Class flow

```mermaid
flowchart TB
  UI[WayfireNetworkInfo tray + NetworkControlWidget]
  NM[NetworkManager]
  F[NetworkBackendFactory]
  B[NetworkBackendBuilder]
  FB[FreeBSDNetworkBackend]
  LN[Linux: NM D-Bus in manager]
  PR[wf_net::probe_interfaces]
  IF[getifaddrs]
  RT[route get default]
  IC[ifconfig detail]

  UI --> NM
  NM --> F
  F --> B
  B -->|freebsd| FB
  B -->|linux| LN
  FB --> PR
  PR --> IF
  PR --> RT
  PR --> IC
  FB -->|InterfaceInfo| FN[FreeBSDNetwork]
  FN --> NM
  NM --> UI
```

---

## Source map (current / target)

| File | Role |
|------|------|
| `network-types.hpp/cpp` | Kind, display name, icon, CSS, fingerprint |
| `network-info.hpp/cpp` | FreeBSD probe + pure parsers + test hooks |
| `network-backend.hpp` | `NetworkBackend` + Builder + Factory API |
| `network-backend-factory.cpp` | OS branch |
| `freebsd-backend.cpp` | Poll product, primary signal |
| `freebsd-network.cpp` | Snapshot → `Network` |
| `manager.cpp` | Orchestration; FreeBSD vs D-Bus |
| `panel/widgets/network.cpp` | Tray widget |
| `network-widget.cpp` | Popover (still NM-shaped — rework to match mockup) |

---

## Probe → tray contract

```mermaid
sequenceDiagram
  participant B as FreeBSDNetworkBackend
  participant P as probe_interfaces
  participant M as NetworkManager
  participant T as Tray WayfireNetworkInfo

  B->>P: probe_interfaces(opts)
  P-->>B: InterfaceInfo[]
  B->>B: upsert FreeBSDNetwork by path
  B->>B: pick_primary_path
  B->>M: signal_primary_changed
  M->>T: signal_default_changed Connection
  Note over B: fingerprint equal → no list rebuild
```

---

## Privilege → information-only

```mermaid
flowchart TB
  PF[probe_features]
  PF --> AD[probe_admin_privilege]
  AD --> R{AdminPrivilege}
  R -->|Root / Doas / Sudo| CA[can_admin · ready]
  R -->|NeedsPassword| PW[can_admin · needs_password]
  R -->|None| IO[information-only]
  CA --> MUT[Configure · Create · up/down · destroy]
  PW --> DLG[Auth dialog on mutation]
  DLG --> MUT
  IO --> RO[List + tooltip only]
```

Input validation (pure): `validate_iface_name`, `validate_ipv4/6_address`,
`validate_prefix_length`, `validate_config_form`, `validate_create_form`,
`validate_admin_password`.

---

## Right-click actions + Create preflight

```mermaid
flowchart LR
  subgraph probe [Live probe]
    IF[InterfaceInfo.up / running]
    DES[is_destroyable_iface]
    C[ifconfig -C]
    K[kldstat + .ko paths]
    AD[can_admin]
  end

  subgraph ui [Popover]
    CTX[Context menu]
    CR[Create modal]
  end

  IF -->|toggle label| CTX
  DES -->|Delete enable| CTX
  AD --> CTX
  AD --> CR
  C --> PF[evaluate_create_preflight]
  K --> PF
  PF -->|can_create only| CR
```

**UI:** list only types with `can_create`; no module/command success text.  
**detail** on `CreatePreflight` is diagnostics (`module_unavailable`, …), not chrome.

| Pure (gtest) | Live probe | Apply (deferred) |
|--------------|------------|------------------|
| `is_destroyable_iface` | flags from `getifaddrs` | elevate + ifconfig |
| `evaluate_create_preflight` | `probe_create_preflight` | elevate + create |
| `parse_ifconfig_clone_list` | `ifconfig -C` | — |
| `kldstat_has_module` | `kldstat` | optional kldload |

Domain types: `CloneTypeInfo`, `CreatePreflight` in `network-types.hpp`.

---

## Wi‑Fi (v2, optional module)

```mermaid
flowchart LR
  W{wlanN present?}
  W -->|no| H[Hide Wi-Fi section]
  W -->|yes| C{wpa_cli works?}
  C -->|no| H2[Show iface only]
  C -->|yes| S[Scan / status / connect]
```

---

## UI snapshots (SVG)

| Diagram | Content |
|---------|---------|
| [diagrams/tray-icon-only.svg](diagrams/tray-icon-only.svg) | Tray icon only |
| [diagrams/popover-interfaces.svg](diagrams/popover-interfaces.svg) | Interface list + Configure/Create |
| [diagrams/popover-context-menu.svg](diagrams/popover-context-menu.svg) | Right-click actions |
| [diagrams/modal-configure.svg](diagrams/modal-configure.svg) | Address editor modal |
| [diagrams/modal-create-preflight.svg](diagrams/modal-create-preflight.svg) | Create (available types only) |
| [diagrams/modal-auth-password.svg](diagrams/modal-auth-password.svg) | Password when elevator needs it |
| [diagrams/information-only.svg](diagrams/information-only.svg) | No admin privileges |

Interactive twin: [mockup.html](mockup.html).

---

## Tests

```sh
meson test -C build --suite unit   # includes network-backend-test
# Line coverage of pure/probe units (like audio):
docs/network-control/tests/coverage.sh
```

**Covered (~96% of pure/probe .cpp):** classify, display/icon/css, fingerprint,
destroyable, clone/preflight, input validation, route/ifconfig parse, primary pick,
admin privilege + create preflight via `InfoHooks`, FreeBSDNetwork snapshot, factory.

**Not unit-covered (integration):** `network-widget.cpp`, `manager.cpp` NM D-Bus,
panel tray chrome. Residual misses: `geteuid()==0`, popen/getifaddrs hard failures.
