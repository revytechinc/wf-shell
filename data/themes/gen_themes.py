#!/usr/bin/env python3.12
"""Generate wf-shell panel themes with guaranteed text/icon contrast.

Each theme is a palette + style (classic3d | soft | neon). Output CSS never
pairs accent-colored text on accent-colored fills.
"""
from __future__ import annotations

from pathlib import Path

OUT = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# Palettes
# ---------------------------------------------------------------------------

THEMES: dict[str, dict] = {
    "win95": {
        "name": "Windows 95",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#c0c0c0",
        "panel_fg": "#000000",
        "panel_border": "#000080",
        "accent": "#000080",
        "accent_fg": "#ffffff",
        "surface": "#c0c0c0",
        "surface_fg": "#000000",
        "surface_alt": "#dfdfdf",
        "field_bg": "#ffffff",
        "field_fg": "#000000",
        "title_bg": "#000080",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#000000",
        "highlight": "#000080",
        "highlight2": "#008080",
        "destructive_bg": "#c0c0c0",
        "destructive_fg": "#800000",
        "destructive_hover_bg": "#800000",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#dfdfdf",
        "shadow": "#808080",
        "shine": "#ffffff",
        "success": "#008000",
        "warn": "#808000",
        "danger": "#800000",
        "dim": "#404040",
        "net_ex": "#008000",
        "net_good": "#000080",
        "net_med": "#808000",
        "net_weak": "#800000",
        "net_none": "#808080",
        "radius": "0",
        "glow": "none",
    },
    "system7": {
        "name": "System 7",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#dddddd",
        "panel_fg": "#000000",
        "panel_border": "#000000",
        "accent": "#000000",
        "accent_fg": "#ffffff",
        "surface": "#eeeeee",
        "surface_fg": "#000000",
        "surface_alt": "#ffffff",
        "field_bg": "#ffffff",
        "field_fg": "#000000",
        "title_bg": "#000000",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#222222",
        "highlight": "#000000",
        "highlight2": "#666666",
        "destructive_bg": "#eeeeee",
        "destructive_fg": "#990000",
        "destructive_hover_bg": "#990000",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#eeeeee",
        "shadow": "#888888",
        "shine": "#ffffff",
        "success": "#006600",
        "warn": "#886600",
        "danger": "#990000",
        "dim": "#444444",
        "net_ex": "#006600",
        "net_good": "#000000",
        "net_med": "#886600",
        "net_weak": "#990000",
        "net_none": "#888888",
        "radius": "2px",
        "glow": "none",
    },
    "amiga-workbench": {
        "name": "Amiga Workbench",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#9999bb",
        "panel_fg": "#000000",
        "panel_border": "#000000",
        "accent": "#0000aa",
        "accent_fg": "#ffffff",
        "surface": "#aaaaaa",
        "surface_fg": "#000000",
        "surface_alt": "#bbbbbb",
        "field_bg": "#ffffff",
        "field_fg": "#000000",
        "title_bg": "#0000aa",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#000022",
        "highlight": "#0000aa",
        "highlight2": "#ee8800",
        "destructive_bg": "#aaaaaa",
        "destructive_fg": "#000000",
        "destructive_hover_bg": "#ee8800",
        "destructive_hover_fg": "#000000",
        "well_bg": "#b8b8cc",
        "shadow": "#555555",
        "shine": "#ffffff",
        "success": "#006600",
        "warn": "#886600",
        "danger": "#aa0000",
        "dim": "#333333",
        "net_ex": "#006600",
        "net_good": "#0000aa",
        "net_med": "#886600",
        "net_weak": "#aa4400",
        "net_none": "#444444",
        "radius": "0",
        "glow": "none",
    },
    "crt-phosphor": {
        "name": "CRT Phosphor",
        "era": "retro",
        "style": "neon",
        "panel_bg": "rgba(8, 16, 8, 0.96)",
        "panel_fg": "#33ff66",
        "panel_border": "#33ff66",
        "accent": "#33ff66",
        "accent_fg": "#001100",
        "surface": "rgba(6, 14, 6, 0.98)",
        "surface_fg": "#66ff99",
        "surface_alt": "rgba(12, 28, 12, 0.95)",
        "field_bg": "#001a00",
        "field_fg": "#66ff99",
        "title_bg": "#0a220a",
        "title_fg": "#33ff66",
        "icon": "#33ff66",
        "icon_outline": "#001100",
        "icon_on_accent": "#001100",
        "icon_on_accent_outline": "#33ff66",
        "meter_bg": "#001100",
        "highlight": "#33ff66",
        "highlight2": "#99ff99",
        "destructive_bg": "rgba(40, 0, 0, 0.5)",
        "destructive_fg": "#ff6666",
        "destructive_hover_bg": "#660000",
        "destructive_hover_fg": "#ffaaaa",
        "well_bg": "rgba(0, 40, 0, 0.45)",
        "shadow": "#003300",
        "shine": "#66ff99",
        "success": "#33ff66",
        "warn": "#ccff33",
        "danger": "#ff4444",
        "dim": "#2a8833",
        "net_ex": "#33ff66",
        "net_good": "#66ff99",
        "net_med": "#ccff33",
        "net_weak": "#ffaa33",
        "net_none": "#226622",
        "radius": "2px",
        "glow": "0 0 8px rgba(51, 255, 102, 0.35)",
    },
    "synthwave": {
        "name": "Synthwave 84",
        "era": "retro",
        "style": "neon",
        "panel_bg": "rgba(18, 12, 32, 0.96)",
        "panel_fg": "#f8f8f2",
        "panel_border": "#ff6ac1",
        "accent": "#ff6ac1",
        "accent_fg": "#1a0a18",
        "surface": "rgba(22, 16, 40, 0.98)",
        "surface_fg": "#f8f8f2",
        "surface_alt": "rgba(36, 24, 56, 0.95)",
        "field_bg": "#1a1230",
        "field_fg": "#f8f8f2",
        "title_bg": "#2a1848",
        "title_fg": "#ff6ac1",
        "icon": "#f9e2af",
        "icon_outline": "#1a0a18",
        "icon_on_accent": "#1a0a18",
        "icon_on_accent_outline": "#ff6ac1",
        "meter_bg": "#0c0818",
        "highlight": "#ff6ac1",
        "highlight2": "#00d4ff",
        "destructive_bg": "rgba(80, 20, 40, 0.5)",
        "destructive_fg": "#ff8fa3",
        "destructive_hover_bg": "#aa2255",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "rgba(40, 20, 60, 0.55)",
        "shadow": "#0a0614",
        "shine": "#ff6ac1",
        "success": "#7ee787",
        "warn": "#f9e2af",
        "danger": "#ff6ac1",
        "dim": "#a89bc9",
        "net_ex": "#7ee787",
        "net_good": "#00d4ff",
        "net_med": "#f9e2af",
        "net_weak": "#ff6ac1",
        "net_none": "#6a5a8a",
        "radius": "6px",
        "glow": "0 0 12px rgba(255, 106, 193, 0.35)",
    },
    "miami-cyberpunk": {
        "name": "Miami Cyberpunk",
        "era": "modern",
        "style": "neon",
        "panel_bg": "rgba(10, 8, 19, 0.96)",
        "panel_fg": "#e0def4",
        "panel_border": "#ff007f",
        "accent": "#ff007f",
        "accent_fg": "#120018",
        "surface": "rgba(14, 12, 28, 0.98)",
        "surface_fg": "#e0def4",
        "surface_alt": "rgba(22, 18, 40, 0.95)",
        "field_bg": "#120e22",
        "field_fg": "#e0def4",
        "title_bg": "#1a1030",
        "title_fg": "#00f0ff",
        "icon": "#00f0ff",
        "icon_outline": "#0a0614",
        "icon_on_accent": "#120018",
        "icon_on_accent_outline": "#ff007f",
        "meter_bg": "#07050e",
        "highlight": "#ff007f",
        "highlight2": "#00f0ff",
        "destructive_bg": "rgba(80, 10, 40, 0.4)",
        "destructive_fg": "#ff6b9d",
        "destructive_hover_bg": "#aa0044",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "rgba(20, 16, 40, 0.55)",
        "shadow": "#05040a",
        "shine": "#00f0ff",
        "success": "#7ee787",
        "warn": "#f9e2af",
        "danger": "#ff007f",
        "dim": "#908caa",
        "net_ex": "#7ee787",
        "net_good": "#00f0ff",
        "net_med": "#f9e2af",
        "net_weak": "#ff9f43",
        "net_none": "#6c7086",
        "radius": "8px",
        "glow": "0 0 14px rgba(255, 0, 127, 0.35)",
    },
    "nord": {
        "name": "Nord",
        "era": "modern",
        "style": "soft",
        "panel_bg": "rgba(46, 52, 64, 0.96)",
        "panel_fg": "#eceff4",
        "panel_border": "#88c0d0",
        "accent": "#88c0d0",
        "accent_fg": "#2e3440",
        "surface": "rgba(59, 66, 82, 0.98)",
        "surface_fg": "#eceff4",
        "surface_alt": "rgba(67, 76, 94, 0.95)",
        "field_bg": "#3b4252",
        "field_fg": "#eceff4",
        "title_bg": "#3b4252",
        "title_fg": "#88c0d0",
        "icon": "#d8dee9",
        "icon_outline": "#2e3440",
        "icon_on_accent": "#2e3440",
        "icon_on_accent_outline": "#88c0d0",
        "meter_bg": "#2e3440",
        "highlight": "#88c0d0",
        "highlight2": "#a3be8c",
        "destructive_bg": "rgba(191, 97, 106, 0.2)",
        "destructive_fg": "#bf616a",
        "destructive_hover_bg": "#bf616a",
        "destructive_hover_fg": "#eceff4",
        "well_bg": "rgba(59, 66, 82, 0.7)",
        "shadow": "#2e3440",
        "shine": "#d8dee9",
        "success": "#a3be8c",
        "warn": "#ebcb8b",
        "danger": "#bf616a",
        "dim": "#a0a8b8",
        "net_ex": "#a3be8c",
        "net_good": "#88c0d0",
        "net_med": "#ebcb8b",
        "net_weak": "#d08770",
        "net_none": "#4c566a",
        "radius": "6px",
        "glow": "none",
    },
    "catppuccin-mocha": {
        "name": "Catppuccin Mocha",
        "era": "modern",
        "style": "soft",
        "panel_bg": "rgba(24, 24, 37, 0.96)",
        "panel_fg": "#cdd6f4",
        "panel_border": "#cba6f7",
        "accent": "#cba6f7",
        "accent_fg": "#1e1e2e",
        "surface": "rgba(30, 30, 46, 0.98)",
        "surface_fg": "#cdd6f4",
        "surface_alt": "rgba(49, 50, 68, 0.95)",
        "field_bg": "#313244",
        "field_fg": "#cdd6f4",
        "title_bg": "#313244",
        "title_fg": "#cba6f7",
        "icon": "#cdd6f4",
        "icon_outline": "#11111b",
        "icon_on_accent": "#1e1e2e",
        "icon_on_accent_outline": "#cba6f7",
        "meter_bg": "#11111b",
        "highlight": "#cba6f7",
        "highlight2": "#89b4fa",
        "destructive_bg": "rgba(243, 139, 168, 0.18)",
        "destructive_fg": "#f38ba8",
        "destructive_hover_bg": "#f38ba8",
        "destructive_hover_fg": "#1e1e2e",
        "well_bg": "rgba(49, 50, 68, 0.65)",
        "shadow": "#11111b",
        "shine": "#cdd6f4",
        "success": "#a6e3a1",
        "warn": "#f9e2af",
        "danger": "#f38ba8",
        "dim": "#a6adc8",
        "net_ex": "#a6e3a1",
        "net_good": "#89b4fa",
        "net_med": "#f9e2af",
        "net_weak": "#fab387",
        "net_none": "#6c7086",
        "radius": "8px",
        "glow": "none",
    },
    "tokyo-night": {
        "name": "Tokyo Night",
        "era": "modern",
        "style": "soft",
        "panel_bg": "rgba(22, 22, 30, 0.96)",
        "panel_fg": "#c0caf5",
        "panel_border": "#7aa2f7",
        "accent": "#7aa2f7",
        "accent_fg": "#1a1b26",
        "surface": "rgba(26, 27, 38, 0.98)",
        "surface_fg": "#c0caf5",
        "surface_alt": "rgba(36, 40, 59, 0.95)",
        "field_bg": "#24283b",
        "field_fg": "#c0caf5",
        "title_bg": "#24283b",
        "title_fg": "#7aa2f7",
        "icon": "#c0caf5",
        "icon_outline": "#16161e",
        "icon_on_accent": "#1a1b26",
        "icon_on_accent_outline": "#7aa2f7",
        "meter_bg": "#16161e",
        "highlight": "#7aa2f7",
        "highlight2": "#bb9af7",
        "destructive_bg": "rgba(247, 118, 142, 0.18)",
        "destructive_fg": "#f7768e",
        "destructive_hover_bg": "#f7768e",
        "destructive_hover_fg": "#1a1b26",
        "well_bg": "rgba(36, 40, 59, 0.65)",
        "shadow": "#16161e",
        "shine": "#c0caf5",
        "success": "#9ece6a",
        "warn": "#e0af68",
        "danger": "#f7768e",
        "dim": "#a9b1d6",
        "net_ex": "#9ece6a",
        "net_good": "#7aa2f7",
        "net_med": "#e0af68",
        "net_weak": "#ff9e64",
        "net_none": "#565f89",
        "radius": "8px",
        "glow": "none",
    },
    "rose-pine": {
        "name": "Rose Pine",
        "era": "modern",
        "style": "soft",
        "panel_bg": "rgba(25, 23, 36, 0.96)",
        "panel_fg": "#e0def4",
        "panel_border": "#eb6f92",
        "accent": "#eb6f92",
        "accent_fg": "#191724",
        "surface": "rgba(31, 29, 46, 0.98)",
        "surface_fg": "#e0def4",
        "surface_alt": "rgba(38, 35, 58, 0.95)",
        "field_bg": "#26233a",
        "field_fg": "#e0def4",
        "title_bg": "#26233a",
        "title_fg": "#c4a7e7",
        "icon": "#e0def4",
        "icon_outline": "#191724",
        "icon_on_accent": "#191724",
        "icon_on_accent_outline": "#eb6f92",
        "meter_bg": "#191724",
        "highlight": "#eb6f92",
        "highlight2": "#9ccfd8",
        "destructive_bg": "rgba(235, 111, 146, 0.18)",
        "destructive_fg": "#eb6f92",
        "destructive_hover_bg": "#eb6f92",
        "destructive_hover_fg": "#191724",
        "well_bg": "rgba(38, 35, 58, 0.65)",
        "shadow": "#191724",
        "shine": "#e0def4",
        "success": "#9ccfd8",
        "warn": "#f6c177",
        "danger": "#eb6f92",
        "dim": "#908caa",
        "net_ex": "#9ccfd8",
        "net_good": "#c4a7e7",
        "net_med": "#f6c177",
        "net_weak": "#eb6f92",
        "net_none": "#6e6a86",
        "radius": "8px",
        "glow": "none",
    },
    "dracula": {
        "name": "Dracula",
        "era": "modern",
        "style": "soft",
        "panel_bg": "rgba(40, 42, 54, 0.96)",
        "panel_fg": "#f8f8f2",
        "panel_border": "#bd93f9",
        "accent": "#bd93f9",
        "accent_fg": "#282a36",
        "surface": "rgba(40, 42, 54, 0.98)",
        "surface_fg": "#f8f8f2",
        "surface_alt": "rgba(68, 71, 90, 0.95)",
        "field_bg": "#44475a",
        "field_fg": "#f8f8f2",
        "title_bg": "#44475a",
        "title_fg": "#bd93f9",
        "icon": "#f8f8f2",
        "icon_outline": "#21222c",
        "icon_on_accent": "#282a36",
        "icon_on_accent_outline": "#bd93f9",
        "meter_bg": "#21222c",
        "highlight": "#bd93f9",
        "highlight2": "#8be9fd",
        "destructive_bg": "rgba(255, 85, 85, 0.2)",
        "destructive_fg": "#ff5555",
        "destructive_hover_bg": "#ff5555",
        "destructive_hover_fg": "#f8f8f2",
        "well_bg": "rgba(68, 71, 90, 0.65)",
        "shadow": "#21222c",
        "shine": "#f8f8f2",
        "success": "#50fa7b",
        "warn": "#f1fa8c",
        "danger": "#ff5555",
        "dim": "#6272a4",
        "net_ex": "#50fa7b",
        "net_good": "#8be9fd",
        "net_med": "#f1fa8c",
        "net_weak": "#ffb86c",
        "net_none": "#6272a4",
        "radius": "6px",
        "glow": "none",
    },
}


def icon_shadow(outline: str, extra: str = "") -> str:
    o = outline
    parts = [
        f"0 1px 0 {o}",
        f"0 -1px 0 {o}",
        f"1px 0 0 {o}",
        f"-1px 0 0 {o}",
        f"1px 1px 0 {o}",
        f"-1px -1px 0 {o}",
        f"1px -1px 0 {o}",
        f"-1px 1px 0 {o}",
    ]
    if extra:
        parts.append(extra)
    return ",\n        ".join(parts)


def border_3d(p: dict, raised: bool = True) -> str:
    if p["style"] != "classic3d":
        return f"""    border: 1px solid {p['panel_border']};
    border-radius: {p['radius']};"""
    if raised:
        return f"""    border: 2px solid {p['panel_fg']};
    border-top-color: {p['shine']};
    border-left-color: {p['shine']};
    border-right-color: {p['shadow']};
    border-bottom-color: {p['shadow']};
    border-radius: {p['radius']};"""
    return f"""    border: 2px solid {p['panel_fg']};
    border-top-color: {p['shadow']};
    border-left-color: {p['shadow']};
    border-right-color: {p['shine']};
    border-bottom-color: {p['shine']};
    border-radius: {p['radius']};"""


def box_glow(p: dict) -> str:
    g = p.get("glow") or "none"
    if g == "none":
        return "    box-shadow: none;"
    return f"    box-shadow: {g};"


def render(theme_id: str, p: dict) -> str:
    is_3d = p["style"] == "classic3d"
    soft_radius = p["radius"]
    icon_sh = icon_shadow(p["icon_outline"], f"1px 1px 1px rgba(0,0,0,0.45)" if not is_3d else "")
    icon_sh_acc = icon_shadow(p["icon_on_accent_outline"])
    bevel = border_3d(p, True)
    sunken = border_3d(p, False)
    glow = box_glow(p)

    panel_extra = ""
    if is_3d:
        panel_extra = f"""
    border-top: 1px solid {p['shine']};
    box-shadow: inset 0 1px 0 {p['shine']}, 0 2px 0 {p['shadow']};"""
    else:
        panel_extra = f"""
{glow}"""

    return f"""/* wf-shell-theme: id={theme_id}; name={p['name']}; era={p['era']} */
/* ==========================================================================
 * {p['name']} — contrast-safe theme (generated)
 * panel_fg on panel_bg · surface_fg on surface · title_fg on title_bg
 * Icons always outlined so they never vanish into the bar.
 * ========================================================================== */

/* ── Panel ─────────────────────────────────────────────────────────────── */

.wf-panel {{
    background-color: {p['panel_bg']};
    background-image: none;
    color: {p['panel_fg']};
    border-bottom: 2px solid {p['panel_border']};{panel_extra}
}}

.wf-panel .clock,
.wf-panel .battery,
.wf-panel .network,
.wf-panel .volume,
.wf-panel .menu,
.wf-panel .app-button,
.wf-panel .window-list button,
.wf-panel .tray,
.wf-panel label {{
    color: {p['panel_fg']};
    font-weight: bold;
    text-shadow: none;
    border: 1px solid transparent;
    border-radius: {soft_radius};
    padding: 2px 6px;
    margin: 1px;
}}

.wf-panel .clock:hover,
.wf-panel .battery:hover,
.wf-panel .network:hover,
.wf-panel .volume:hover,
.wf-panel .menu:hover,
.wf-panel .app-button:hover,
.wf-panel .window-list button:hover {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

.wf-panel .volume.selected,
.wf-panel .network.selected,
.wf-panel .menu.selected,
.wf-panel .window-list button.active,
.wf-panel .window-button.activated {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

.wf-panel .window-button:hover {{
    background-color: {p['surface_alt']};
    color: {p['surface_fg']};
}}

/* Icon wells — inspectable bounds for tray gadgets */
.wf-panel .volume,
.wf-panel .network,
.wf-panel .battery,
.wf-panel .menu,
.wf-panel .tray-button {{
    background-color: {p['well_bg']};
{bevel}
    padding: 2px 6px;
    margin: 1px 2px;
}}

.wf-panel .volume:hover,
.wf-panel .network:hover,
.wf-panel .battery:hover,
.wf-panel .menu:hover,
.wf-panel .tray-button:hover {{
    background-color: {p['accent']};
    color: {p['accent_fg']};
}}

/* Symbolic icons + outline */
.wf-panel .widget-icon,
.wf-panel .volume image,
.wf-panel .network image,
.wf-panel .battery image,
.wf-panel .menu image,
.wf-panel .tray-button image,
.wf-panel .clock image,
.wf-panel image.widget-icon {{
    color: {p['icon']};
    -gtk-icon-shadow:
        {icon_sh};
}}

.wf-panel .volume.selected image,
.wf-panel .network.selected image,
.wf-panel .menu.selected image,
.wf-panel .volume:hover image,
.wf-panel .network:hover image,
.wf-panel .menu:hover image,
.wf-panel .battery:hover image {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

.wf-panel .network.excellent image {{ color: {p['net_ex']}; }}
.wf-panel .network.good image      {{ color: {p['net_good']}; }}
.wf-panel .network.medium image    {{ color: {p['net_med']}; }}
.wf-panel .network.weak image      {{ color: {p['net_weak']}; }}
.wf-panel .network.none image      {{ color: {p['net_none']}; }}
.wf-panel .network.wifi.weak image {{ color: {p['net_weak']}; }}
.wf-panel .network.wifi.excellent image {{ color: {p['net_ex']}; }}

/* Full-color launchers */
.wf-panel image.launcher,
.wf-panel .launcher image,
.wf-panel .launchers image {{
    filter:
        drop-shadow(0 0 0.6px {p['icon_outline']})
        drop-shadow(0 0 0.6px {p['icon_outline']})
        drop-shadow(1px 1px 0 rgba(0, 0, 0, 0.55));
}}

.wf-panel .launcher-button,
.wf-panel .launcher {{
    margin: 0 3px;
    padding: 2px;
    background-color: {p['well_bg']};
{bevel}
}}

.wf-panel .launcher-button:hover,
.wf-panel .launcher:hover {{
    background-color: {p['surface_alt']};
}}

.wf-panel .battery overlay label {{
    color: {p['panel_fg']};
    text-shadow:
         1px  1px 0 {p['icon_outline']},
        -1px -1px 0 {p['icon_outline']},
        -1px  1px 0 {p['icon_outline']},
         1px -1px 0 {p['icon_outline']},
         0     1px 0 {p['icon_outline']},
         0    -1px 0 {p['icon_outline']},
        -1px   0   0 {p['icon_outline']},
         1px   0   0 {p['icon_outline']};
    font-weight: bold;
}}

/* ── Popovers / menus ──────────────────────────────────────────────────── */

popover > contents,
popovercontents,
.popover > contents,
popovermenu contents,
popovermenu > contents {{
    background-color: {p['surface']};
    background-image: none;
{bevel if is_3d else f"    border: 2px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    padding: 10px;
    color: {p['surface_fg']};
{glow if not is_3d else f"    box-shadow: 4px 4px 0 rgba(0, 0, 0, 0.3);"}
}}

.popover label,
popover label,
popovermenu label {{
    color: {p['surface_fg']};
}}

.popover button,
.network-control-center button,
window button {{
    background-image: none;
    background-color: {p['surface_alt']};
    background: {p['surface_alt']};
{bevel}
    color: {p['surface_fg']};
    padding: 4px 10px;
    font-weight: bold;
    text-shadow: none;
    box-shadow: none;
}}

.popover button:hover,
.network-control-center button:hover,
window button:hover {{
    background-image: none;
    background-color: {p['accent']};
    background: {p['accent']};
    border-color: {p['panel_border']};
    color: {p['accent_fg']};
    text-shadow: none;
    box-shadow: none;
}}

.popover button:active,
.network-control-center button:active,
window button:active {{
    background-color: {p['field_bg']};
{sunken}
    color: {p['surface_fg']};
}}

.popover button.destructive-action,
.network-control-center button.destructive-action {{
    background-image: none;
    background-color: {p['destructive_bg']};
    background: {p['destructive_bg']};
    border-color: {p['danger']};
    color: {p['destructive_fg']};
}}

.popover button.destructive-action:hover,
.network-control-center button.destructive-action:hover {{
    background-image: none;
    background-color: {p['destructive_hover_bg']};
    background: {p['destructive_hover_bg']};
    border-color: {p['panel_border']};
    color: {p['destructive_hover_fg']};
    text-shadow: none;
    box-shadow: none;
}}

.popover button.flat,
.network-control-center button.flat {{
    background-image: none;
    background-color: transparent;
    background: transparent;
    border: 1px solid transparent;
    color: {p['surface_fg']};
    box-shadow: none;
}}

.popover button.flat:hover,
.network-control-center button.flat:hover {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    background: {p['accent']};
    border-color: {p['panel_border']};
    box-shadow: none;
}}

.popover button image,
.network-control-center button image,
.popover .widget-icon {{
    color: {p['icon']};
    -gtk-icon-shadow:
        {icon_sh};
}}

.popover button:hover image,
.network-control-center button:hover image,
.popover button.flat:hover image {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

/* Combos / fields — always surface_fg on field_bg */
.popover combobox,
.network-control-center combobox,
.network-control-center combobox button,
.volume-popover-combo,
.volume-popover-device-combo,
combobox,
combobox button,
combobox > box > button,
dropdown,
dropdown button {{
    background-image: none;
    background-color: {p['field_bg']};
    background: {p['field_bg']};
{sunken if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    color: {p['field_fg']};
    text-shadow: none;
    font-weight: bold;
    box-shadow: none;
    padding: 4px 8px;
}}

.volume-popover-combo:hover,
.volume-popover-device-combo:hover,
combobox button:hover,
dropdown button:hover {{
    background-image: none;
    background-color: {p['surface_alt']};
    background: {p['surface_alt']};
    border-color: {p['accent']};
    color: {p['surface_fg']};
    box-shadow: none;
}}

combobox cellview,
combobox label,
dropdown label,
dropdown cellview {{
    color: {p['field_fg']};
    text-shadow: none;
}}

popover listbox row,
popover listview row,
popover row,
.popover listbox row,
popovermenu modelbutton,
popover modelbutton {{
    background-color: transparent;
    color: {p['surface_fg']};
    font-weight: bold;
    padding: 5px 10px;
    margin: 1px 0;
    border-radius: {soft_radius};
    text-shadow: none;
}}

popover listbox row:hover,
popover listview row:hover,
popover row:hover,
.popover listbox row:hover,
popovermenu modelbutton:hover,
popover modelbutton:hover,
popover listbox row:selected,
popover listview row:selected,
popover row:selected,
.popover listbox row:selected,
popovermenu modelbutton:selected,
popover modelbutton:selected {{
    background-color: {p['accent']};
    color: {p['accent_fg']};
    text-shadow: none;
}}

popover listview,
popover listbox,
popover list,
.popover listbox {{
    background-color: transparent;
    background: transparent;
    border: none;
}}

/* ── Volume ────────────────────────────────────────────────────────────── */

.volume-popover-root {{
    background-color: {p['surface']};
    color: {p['surface_fg']};
}}

/*
 * Title bar on the HEADER box (not the Label). GtkLabel often ignores
 * background-color, so white title_fg on a light surface was unreadable
 * (Win95 silver + white "Sound Settings").
 */
.volume-popover-header {{
    background-color: {p['title_bg']};
    background-image: none;
    color: {p['title_fg']};
    border: 1px solid {p['panel_border']};
    border-radius: {soft_radius};
    padding: 6px 10px;
    margin: 0 0 4px 0;
    min-height: 1.6em;
}}

.volume-popover-title {{
    font-size: 1.15em;
    font-weight: 900;
    color: {p['title_fg']};
    background-color: transparent;
    background-image: none;
    padding: 0;
    border: none;
    text-shadow: none;
    /* Markup <b> must inherit the title bar foreground */
    opacity: 1.0;
}}

.volume-popover-header label,
.volume-popover-title,
.volume-popover-title * {{
    color: {p['title_fg']};
}}

.volume-popover-section-title {{
    font-size: 0.78em;
    font-weight: 800;
    color: {p['dim']};
    text-shadow: none;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    opacity: 1.0; /* override default.css dim opacity */
}}

.volume-popover-section {{
    background-color: {p['surface_alt']};
{bevel if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    padding: 6px;
    margin: 4px 0;
}}

.volume-popover-mute-btn {{
    border-radius: {soft_radius};
    padding: 6px;
    background-image: none;
    background-color: {p['surface_alt']};
    background: {p['surface_alt']};
{bevel}
    color: {p['surface_fg']};
}}

.volume-popover-mute-btn image {{
    color: {p['icon']};
    -gtk-icon-shadow:
        {icon_sh};
}}

.volume-popover-mute-btn:hover {{
    background-image: none;
    background-color: {p['accent']};
    background: {p['accent']};
    border-color: {p['panel_border']};
    color: {p['accent_fg']};
    transform: none;
    box-shadow: none;
}}

.volume-popover-mute-btn:hover image {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

.volume-popover-pct {{
    font-size: 0.95em;
    font-weight: bold;
    color: {p['surface_fg']};
    text-shadow: none;
}}

.volume-popover-scale trough {{
    background-color: {p['field_bg']};
    min-height: 8px;
    border-radius: {soft_radius};
    border: 1px solid {p['shadow']};
}}

.volume-popover-scale highlight {{
    border-radius: {soft_radius};
    background-color: {p['highlight']};
    box-shadow: none;
}}

.volume-popover-section:nth-child(5) .volume-popover-scale highlight {{
    background-color: {p['highlight2']};
    box-shadow: none;
}}

.volume-popover-scale slider {{
    background-color: {p['surface_alt']};
{bevel}
    min-width: 14px;
    min-height: 14px;
    margin: -4px;
    box-shadow: none;
}}

.volume-popover-scale slider:hover {{
    transform: none;
    background-color: {p['accent']};
}}

.volume-popover-meter {{
    border-radius: {soft_radius};
{sunken if is_3d else f"    border: 1px solid {p['panel_border']};"}
    margin: 6px 0;
    background-color: {p['meter_bg']};
}}

.volume-popover-adv-btn {{
    border-radius: {soft_radius};
    padding: 5px 14px;
    color: {p['surface_fg']};
    text-shadow: none;
    background-color: {p['surface_alt']};
{bevel}
    font-weight: bold;
    font-size: 0.85em;
}}

.volume-popover-adv-btn:hover {{
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    color: {p['accent_fg']};
    box-shadow: none;
    transform: none;
}}

.dim-label {{
    color: {p['dim']};
    opacity: 1.0;
}}

.volume-popover-voss-section,
.volume-popover-voss-title,
.volume-popover-voss-lbl,
.volume-popover-voss-fmt {{
    color: {p['surface_fg']};
}}

/* ── Network ───────────────────────────────────────────────────────────── */

.network-global-toggles {{
    padding: 8px 12px;
    margin-bottom: 10px;
    background-color: {p['surface_alt']};
{bevel if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
}}

.network-toggle-button {{
    margin-right: 14px;
    color: {p['surface_fg']};
    text-shadow: none;
    font-weight: bold;
    font-size: 0.9em;
}}

.network-toggle-button check {{
    border-radius: {soft_radius};
    border-color: {p['panel_border']};
    background-color: {p['field_bg']};
}}

.network-toggle-button check:checked {{
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    box-shadow: none;
}}

.network-control-center .device,
.network-control-center .freebsd-iface {{
    margin-bottom: 8px;
    background-color: {p['surface_alt']};
{bevel if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    padding: 8px;
}}

.network-control-center .freebsd-iface:hover {{
    background-color: {p['field_bg']};
}}

.network-control-center .device-label {{
    font-weight: bold;
    color: {p['surface_fg']};
    text-shadow: none;
}}

.network-control-center .freebsd-iface .sub,
.network-control-center .freebsd-iface .speed {{
    color: {p['dim']};
}}

.network-control-center .freebsd-iface.is-up .state-dot {{
    background-color: {p['success']};
}}

.network-control-center .freebsd-iface.is-down .state-dot {{
    background-color: {p['danger']};
}}

.network-control-center .access-point {{
    padding: 6px 8px;
    margin: 2px 0;
    border-radius: {soft_radius};
    background-color: transparent;
    border: 1px solid transparent;
}}

.network-control-center .access-point:hover {{
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    box-shadow: none;
}}

.network-control-center .access-point:hover label,
.network-control-center .access-point:hover .band {{
    color: {p['accent_fg']};
}}

.network-control-center .access-point label {{
    color: {p['surface_fg']};
    font-size: 0.95em;
}}

.network-control-center .access-point .band {{
    color: {p['dim']};
    opacity: 1.0;
    font-size: 0.85em;
}}

.network-control-center .access-point.connected,
.network-control-center .access-point.active {{
    font-weight: bold;
    background-color: {p['accent']};
    border: 1px solid {p['panel_border']};
    border-left: 4px solid {p['highlight2']};
    padding-left: 8px;
}}

.network-control-center .access-point.connected label,
.network-control-center .access-point.connected .ap-connected-badge {{
    color: {p['accent_fg']};
    text-shadow: none;
}}

.network-control-center .access-point.saved .ap-saved-badge {{
    color: {p['accent_fg']};
    background-color: {p['highlight2']};
    padding: 0 4px;
    font-weight: bold;
    font-size: 0.8em;
}}

.network-control-center image.access-point {{
    color: {p['icon']};
    -gtk-icon-shadow:
        {icon_sh};
}}

.network-control-center .access-point:hover image.access-point,
.network-control-center .access-point.connected image.access-point {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

.network-control-center .vpn {{
    padding: 8px 12px;
    margin: 4px 0;
    border-radius: {soft_radius};
    background-color: {p['surface_alt']};
    border: 1px solid {p['panel_border']};
}}

.network-control-center .vpn:hover {{
    background-color: {p['accent']};
    border-color: {p['panel_border']};
}}

.network-control-center .vpn:hover label {{
    color: {p['accent_fg']};
}}

.network-control-center .vpn.active {{
    font-weight: bold;
    background-color: {p['accent']};
    border: 1px solid {p['panel_border']};
    border-left: 4px solid {p['highlight2']};
}}

.network-control-center .vpn.active label {{
    color: {p['accent_fg']};
    text-shadow: none;
}}

.network-control-center scrolledwindow,
.network-control-center scrolledwindow viewport {{
    background: transparent;
    background-color: transparent;
    border: none;
    box-shadow: none;
}}

/* ── Windows / dialogs ─────────────────────────────────────────────────── */

window {{
    background-color: {p['surface']};
    color: {p['surface_fg']};
}}

headerbar,
headerbar:backdrop,
window headerbar,
window headerbar:backdrop,
window.dialog headerbar,
window.dialog headerbar:backdrop,
window.popup headerbar,
window.popup headerbar:backdrop,
window.modal headerbar,
window.modal headerbar:backdrop,
.titlebar,
.titlebar:backdrop,
window .titlebar,
window .titlebar:backdrop {{
    background-image: none;
    background-color: {p['title_bg']};
    background: {p['title_bg']};
    color: {p['title_fg']};
    border-bottom: 2px solid {p['panel_border']};
    box-shadow: none;
}}

headerbar label,
headerbar .title,
headerbar:backdrop label,
headerbar:backdrop .title,
window headerbar label,
window headerbar .title,
window headerbar:backdrop label,
window headerbar:backdrop .title,
window.dialog headerbar label,
window.dialog headerbar .title,
window.popup headerbar label,
window.popup headerbar .title {{
    color: {p['title_fg']};
    font-weight: bold;
    text-shadow: none;
}}

headerbar button,
headerbar button image {{
    color: {p['title_fg']};
    -gtk-icon-shadow:
        {icon_shadow(p['icon_outline'] if p['title_fg'] != p['icon_outline'] else p['panel_fg'])};
}}

window .wifi-password-entry {{
    background-color: {p['field_bg']};
{sunken if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    color: {p['field_fg']};
    padding: 6px;
}}

window .wifi-password-entry:focus {{
    border-color: {p['accent']};
    box-shadow: none;
}}

window button.suggested-action {{
    background-image: none;
    background-color: {p['accent']};
    background: {p['accent']};
    color: {p['accent_fg']};
{bevel}
    font-weight: bold;
    box-shadow: none;
}}

window button.suggested-action:hover {{
    background-image: none;
    background-color: {p['highlight2']};
    background: {p['highlight2']};
    color: {p['accent_fg']};
    box-shadow: none;
}}

window button:not(.suggested-action) {{
    background-image: none;
    background-color: {p['surface_alt']};
    background: {p['surface_alt']};
    color: {p['surface_fg']};
{bevel}
}}

window button:not(.suggested-action):hover {{
    background-image: none;
    background-color: {p['accent']};
    background: {p['accent']};
    color: {p['accent_fg']};
    box-shadow: none;
}}

window grid label {{
    color: {p['surface_fg']};
    font-size: 0.95em;
    padding: 4px 6px;
}}

window grid label:first-child {{
    color: {p['surface_fg']};
    font-weight: bold;
    text-shadow: none;
}}

window grid label:nth-child(even) {{
    color: {p['surface_fg']};
    text-shadow: none;
}}

entry,
entry text,
passwordentry,
passwordentry text {{
    background-color: {p['field_bg']};
{sunken if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
    color: {p['field_fg']};
    padding: 6px;
}}

entry:focus,
entry:focus text,
passwordentry:focus,
passwordentry:focus text {{
    border-color: {p['accent']};
    box-shadow: none;
    color: {p['field_fg']};
}}

.popover .app-button,
.menu-popover .app-button {{
    color: {p['surface_fg']};
    background-color: transparent;
}}

.popover .app-button:hover,
.menu-popover .app-button:hover {{
    background-color: {p['accent']};
    color: {p['accent_fg']};
}}

.popover .app-button image {{
    -gtk-icon-shadow:
        {icon_sh};
}}

.popover .app-button:hover image {{
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

.popover combobox label,
.popover .dim-label {{
    color: {p['surface_fg']};
}}
"""


def main() -> None:
    for tid, pal in THEMES.items():
        css = render(tid, pal)
        # Clean up accidental escaped newlines from f-string branch hacks
        css = css.replace("\\n", "\n")
        path = OUT / f"{tid}.css"
        path.write_text(css)
        print(f"wrote {path.name} ({len(css.splitlines())} lines)")


if __name__ == "__main__":
    main()
