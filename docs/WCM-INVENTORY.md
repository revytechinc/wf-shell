# WCM → wf-settings inventory

Source: [WayfireWM/wcm](https://github.com/WayfireWM/wcm) (GTK3).  
Target: **wf-settings** (GTK4) in this tree.

Legend: **Done** | **Partial** | **Planned** | **N/A** (superseded)

## Application shell

| WCM feature | Status | Where in wf-settings |
|-------------|--------|----------------------|
| Main window / plugin grid by category | Done | **Main sidebar**: Common + every plugin under its category (one level) |
| Search plugins | Done | App-level search filters the sidebar |
| Open plugin options page | Done | Selecting a plugin shows **only that plugin’s** options |
| Theme / look like the shell | Done | Loads `default.css` + `panel/css_path` (JSON wins, then INI); reloads on Panel Apply |
| Close button | Done | Window chrome |
| `-c` / `--config` wayfire.ini | Done | `WAYFIRE_CONFIG_FILE` / CLI |
| `-s` / `--shell-config` | Done | `WF_SHELL_CONFIG_FILE` / CLI |
| `-p` / `--plugin` open section | Done | CLI `--plugin=` focuses that sidebar entry |
| Save wayfire config | Done | Window “Save Wayfire” + per-plugin save |
| Save wf-shell config | Done | Dual-write `config.json` + legacy `wf-shell.ini` |
| Output config → wdisplays | **N/A superseded** | **Display** page: discover modes, apply+persist wayfire.ini |

## Config backends

| Feature | Status | Notes |
|---------|--------|-------|
| Load wayfire XML metadata | Done | `ConfigBackend` + `WAYFIRE_PLUGIN_XML_PATH` |
| Load shell XML metadata | Done | `METADATA_DIR` / local share |
| User wayfire.ini | Done | |
| User shell ini | Done | legacy |
| Shell JSON override | Done | `~/.config/wf-shell/config.json` wins over INI |

## Option types (metadata)

| Type | WCM UI | Status | wf-settings handling |
|------|--------|--------|----------------------|
| bool | CheckButton | Done | CheckButton |
| int | Spin / combo labels | Done | SpinButton; labeled enums as DropDown |
| double | Spin | Done | SpinButton |
| string | Entry / file/dir | Done | Entry + Set |
| key | KeyEntry grab | Partial | Entry + Set (string binding) |
| button | KeyEntry | Partial | Same as key string |
| activator | KeyEntry | Partial | Same as key string |
| gesture | Entry | Done | String entry |
| color | ColorButton | Partial | String entry + Set |
| animation | Spin + easing combo | Partial | Full string + Set |
| dynamic-list | Autostart/Bindings UI | Partial | String editor of serialized form |
| output::mode | (via wdisplays) | Done | Display page only advertised modes |
| output::position | wdisplays | Done | Display apply uses pos from probe |

## Plugin lifecycle

| Feature | Status | Notes |
|---------|--------|-------|
| Enable/disable wayfire plugin | Done | Per-plugin “Enabled (core/plugins)” toggle |
| Core plugins always on | Done | core/input/workarounds/output not toggleable off |
| Shell plugins always “on” | Done | Shell sections not in core/plugins list |

## Navigation IA (sensible)

| Layer | Contents |
|-------|----------|
| **Common** | Display, Panel, Desktop, Dock, Sound, Network, Session, AI / MCP |
| **General** | core, input, command, autostart, … |
| **Accessibility** | invert, mag, scale, zoom, … |
| **Desktop** | cube, expo, vswitch, … |
| **Effects** | animate, blur, wobbly, … |
| **Window Management** | grid, move, resize, … |
| **Utility** | ipc, wayfire-shell, … |
| **Shell** | remaining shell sections (panel/dock/background use Common pages) |

No “More options” dump. No “Utility (18)” count badges. Search filters the list.

## Explicitly not cloning

- GTK3 toolkit / gtkmm-3
- Shipping `wdisplays` without persistence
- Nested Advanced paned browser under a single sidebar entry

## Remaining polish

1. Wayland key-grab widget for key/activator
2. ColorButton color picker
3. First-class dynamic-list row editor
