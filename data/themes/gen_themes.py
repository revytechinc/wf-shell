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
    # Borg collective terminal: void black, sickly phosphor green, cold data-cyan.
    # Angular (radius 0), conduit borders, bloom glow — not soft neon candy.
    "crt-phosphor": {
        "name": "CRT Phosphor",
        "era": "retro",
        "style": "neon",
        "panel_bg": "rgba(2, 6, 4, 0.97)",
        "panel_fg": "#39ff14",
        "panel_border": "#1a8f2a",
        "accent": "#39ff14",
        "accent_fg": "#020804",
        "surface": "rgba(3, 8, 5, 0.98)",
        "surface_fg": "#7dff7a",
        "surface_alt": "rgba(6, 16, 10, 0.96)",
        "field_bg": "#010805",
        "field_fg": "#7dff7a",
        "title_bg": "#041208",
        "title_fg": "#39ff14",
        "icon": "#39ff14",
        "icon_outline": "#020804",
        "icon_on_accent": "#020804",
        "icon_on_accent_outline": "#39ff14",
        "meter_bg": "#010402",
        "highlight": "#39ff14",
        "highlight2": "#00e5a8",  # cold collective cyan-green
        "destructive_bg": "rgba(48, 8, 8, 0.55)",
        "destructive_fg": "#ff5555",
        "destructive_hover_bg": "#5a1010",
        "destructive_hover_fg": "#ffcccc",
        "well_bg": "rgba(4, 14, 8, 0.75)",
        "shadow": "#000000",
        "shine": "#7dff7a",
        "success": "#39ff14",
        "warn": "#c8e000",
        "danger": "#ff3333",
        "dim": "#2f6b38",
        "net_ex": "#39ff14",
        "net_good": "#00e5a8",
        "net_med": "#c8e000",
        "net_weak": "#e08020",
        "net_none": "#1a3d22",
        "radius": "0",
        "glow": "0 0 10px rgba(57, 255, 20, 0.28), inset 0 0 12px rgba(0, 40, 15, 0.45)",
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
    "modern-glass": {
        "name": "Modern Glass",
        "era": "modern",
        "style": "glass",
        "panel_bg": "rgba(20, 20, 30, 0.45)",
        "panel_fg": "#ffffff",
        "panel_border": "rgba(255, 255, 255, 0.2)",
        "accent": "rgba(255, 255, 255, 0.22)",
        "accent_fg": "#ffffff",
        "surface": "rgba(15, 15, 25, 0.7)",
        "surface_fg": "#ffffff",
        "surface_alt": "rgba(255, 255, 255, 0.1)",
        "field_bg": "rgba(0, 0, 0, 0.35)",
        "field_fg": "#ffffff",
        "title_bg": "rgba(40, 40, 55, 0.8)",
        "title_fg": "#ffffff",
        "icon": "#ffffff",
        "icon_outline": "rgba(0, 0, 0, 0.45)",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "rgba(0, 0, 0, 0.6)",
        "meter_bg": "rgba(0, 0, 0, 0.5)",
        "highlight": "#38bdf8",
        "highlight2": "#818cf8",
        "destructive_bg": "rgba(239, 68, 68, 0.3)",
        "destructive_fg": "#fca5a5",
        "destructive_hover_bg": "rgba(220, 38, 38, 0.85)",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "rgba(255, 255, 255, 0.08)",
        "shadow": "rgba(0, 0, 0, 0.3)",
        "shine": "rgba(255, 255, 255, 0.35)",
        "success": "#34d399",
        "warn": "#fbbf24",
        "danger": "#f87171",
        "dim": "rgba(255, 255, 255, 0.55)",
        "net_ex": "#34d399",
        "net_good": "#60a5fa",
        "net_med": "#fbbf24",
        "net_weak": "#f87171",
        "net_none": "rgba(255, 255, 255, 0.25)",
        "radius": "10px",
        "glow": "0 8px 32px 0 rgba(0, 0, 0, 0.3)",
    },
    "cde-solaris": {
        "name": "CDE Solaris",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#aeaeae",
        "panel_fg": "#000000",
        "panel_border": "#4b6678",
        "accent": "#4b6678",
        "accent_fg": "#ffffff",
        "surface": "#aeaeae",
        "surface_fg": "#000000",
        "surface_alt": "#bebebe",
        "field_bg": "#8e8e8e",
        "field_fg": "#ffffff",
        "title_bg": "#4b6678",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#3e3e3e",
        "highlight": "#4b6678",
        "highlight2": "#6b8b9b",
        "destructive_bg": "#aeaeae",
        "destructive_fg": "#800000",
        "destructive_hover_bg": "#800000",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#bebebe",
        "shadow": "#5a5a5a",
        "shine": "#ffffff",
        "success": "#008000",
        "warn": "#808000",
        "danger": "#800000",
        "dim": "#4a4a4a",
        "net_ex": "#008000",
        "net_good": "#4b6678",
        "net_med": "#808000",
        "net_weak": "#800000",
        "net_none": "#7a7a7a",
        "radius": "0",
        "glow": "none",
    },
    "sgi-irix": {
        "name": "SGI IRIX",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#7e828c",
        "panel_fg": "#000000",
        "panel_border": "#3b4b66",
        "accent": "#3b4b66",
        "accent_fg": "#ffffff",
        "surface": "#8e929d",
        "surface_fg": "#000000",
        "surface_alt": "#9ea2ad",
        "field_bg": "#5e616b",
        "field_fg": "#ffffff",
        "title_bg": "#3b4b66",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#2e3035",
        "highlight": "#008080",
        "highlight2": "#b23b3b",
        "destructive_bg": "#8e929d",
        "destructive_fg": "#800000",
        "destructive_hover_bg": "#b23b3b",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#6e727c",
        "shadow": "#4e5159",
        "shine": "#bec2cd",
        "success": "#00a080",
        "warn": "#d19a2a",
        "danger": "#b23b3b",
        "dim": "#3e4045",
        "net_ex": "#00a080",
        "net_good": "#3b4b66",
        "net_med": "#d19a2a",
        "net_weak": "#b23b3b",
        "net_none": "#4e5159",
        "radius": "0",
        "glow": "none",
    },
    "ibm-aix": {
        "name": "IBM AIX",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#7f8f9f",
        "panel_fg": "#000000",
        "panel_border": "#1f3f5f",
        "accent": "#1f3f5f",
        "accent_fg": "#ffffff",
        "surface": "#8f9faf",
        "surface_fg": "#000000",
        "surface_alt": "#9fafbf",
        "field_bg": "#ffffff",
        "field_fg": "#000000",
        "title_bg": "#1f3f5f",
        "title_fg": "#ffffff",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#102030",
        "highlight": "#1f3f5f",
        "highlight2": "#3f6f9f",
        "destructive_bg": "#8f9faf",
        "destructive_fg": "#800000",
        "destructive_hover_bg": "#800000",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#738393",
        "shadow": "#4f5f6f",
        "shine": "#cfdfef",
        "success": "#1f8f3f",
        "warn": "#8f7f1f",
        "danger": "#8f1f1f",
        "dim": "#3f4f5f",
        "net_ex": "#1f8f3f",
        "net_good": "#1f3f5f",
        "net_med": "#8f7f1f",
        "net_weak": "#8f1f1f",
        "net_none": "#5f6f7f",
        "radius": "0",
        "glow": "none",
    },
    "beos-haiku": {
        "name": "BeOS Haiku",
        "era": "retro",
        "style": "classic3d",
        "panel_bg": "#ebebeb",
        "panel_fg": "#000000",
        "panel_border": "#3a3a3a",
        "accent": "#3c76c4",
        "accent_fg": "#ffffff",
        "surface": "#ebebeb",
        "surface_fg": "#000000",
        "surface_alt": "#f5f5f5",
        "field_bg": "#ffffff",
        "field_fg": "#000000",
        "title_bg": "#ffd300",
        "title_fg": "#000000",
        "icon": "#000000",
        "icon_outline": "#ffffff",
        "icon_on_accent": "#ffffff",
        "icon_on_accent_outline": "#000000",
        "meter_bg": "#222222",
        "highlight": "#3c76c4",
        "highlight2": "#ffd300",
        "destructive_bg": "#ebebeb",
        "destructive_fg": "#c30000",
        "destructive_hover_bg": "#c30000",
        "destructive_hover_fg": "#ffffff",
        "well_bg": "#dedede",
        "shadow": "#aaaaaa",
        "shine": "#ffffff",
        "success": "#228b22",
        "warn": "#d2b48c",
        "danger": "#c30000",
        "dim": "#555555",
        "net_ex": "#228b22",
        "net_good": "#3c76c4",
        "net_med": "#ffd300",
        "net_weak": "#c30000",
        "net_none": "#aaaaaa",
        "radius": "2px",
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
    if p["style"] == "glass":
        return f"""    border: 1px solid {p['panel_border']};
    border-radius: {p['radius']};
    box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.2), 0 4px 12px {p['shadow']};"""
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
    elif p["style"] == "glass":
        panel_extra = f"""
    background-image: linear-gradient(180deg, rgba(255, 255, 255, 0.08) 0%, rgba(255, 255, 255, 0) 60%, rgba(0, 0, 0, 0.05) 100%);
    backdrop-filter: blur(16px);
    box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.2), 0 8px 32px 0 {p['shadow']};"""
    else:
        blur_rule = ""
        if "rgba" in p['panel_bg']:
            blur_rule = "\n    backdrop-filter: blur(16px);"
        gradient_rule = "\n    background-image: linear-gradient(180deg, rgba(255, 255, 255, 0.03), rgba(0, 0, 0, 0.03));"
        panel_extra = f"""
{glow}{blur_rule}{gradient_rule}"""

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
.wf-panel .tray {{
    color: {p['panel_fg']};
    font-weight: bold;
    text-shadow: none;
    border: 1px solid transparent;
    border-radius: {soft_radius};
    padding: 2px 6px;
    margin: 1px;
    transition: background-color 0.2s ease, color 0.2s ease, border-color 0.2s ease;
}}

/* Default labels on the bar (clock text, etc.) — NOT window-list titles */
.wf-panel .clock label,
.wf-panel .battery label {{
    color: {p['panel_fg']};
    font-weight: bold;
}}

.wf-panel .clock:hover,
.wf-panel .battery:hover,
.wf-panel .network:hover,
.wf-panel .volume:hover,
.wf-panel .menu:hover,
.wf-panel .app-button:hover {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

.wf-panel .volume.selected,
.wf-panel .network.selected,
.wf-panel .menu.selected {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

/*
 * Window list (running apps on the panel).
 * Child labels/images have their own rules (e.g. .wf-panel label) that used to
 * keep panel_fg while the button background became accent — same hue on same
 * hue = unreadable. Always paint button + label + icon together.
 */
.wf-panel .window-list button,
.wf-panel .window-button {{
    color: {p['panel_fg']};
    background-color: transparent;
    font-weight: bold;
    text-shadow: none;
    border: 1px solid transparent;
    border-radius: {soft_radius};
    padding: 2px 6px;
    margin: 1px;
    transition: background-color 0.2s ease, color 0.2s ease, border-color 0.2s ease;
}}

.wf-panel .window-list button label,
.wf-panel .window-button label {{
    color: {p['panel_fg']};
    background-color: transparent;
    text-shadow: none;
    opacity: 1.0;
}}

.wf-panel .window-list button image,
.wf-panel .window-button image {{
    color: {p['icon']};
    -gtk-icon-shadow:
        {icon_sh};
}}

/* Hover: accent plate + contrast foreground on button AND children */
.wf-panel .window-list button:hover,
.wf-panel .window-button:hover {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

.wf-panel .window-list button:hover label,
.wf-panel .window-button:hover label {{
    color: {p['accent_fg']};
    background-color: transparent;
    text-shadow: none;
}}

.wf-panel .window-list button:hover image,
.wf-panel .window-button:hover image {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

/* Active / focused window — same inverted contrast as hover */
.wf-panel .window-list button.active,
.wf-panel .window-button.activated {{
    color: {p['accent_fg']};
    background-color: {p['accent']};
    border-color: {p['panel_border']};
    text-shadow: none;
    box-shadow: none;
}}

.wf-panel .window-list button.active label,
.wf-panel .window-button.activated label,
.wf-panel .window-list button.activated label {{
    color: {p['accent_fg']};
    background-color: transparent;
    text-shadow: none;
    opacity: 1.0;
}}

.wf-panel .window-list button.active image,
.wf-panel .window-button.activated image,
.wf-panel .window-list button.activated image {{
    color: {p['icon_on_accent']};
    -gtk-icon-shadow:
        {icon_sh_acc};
}}

/* Minimized: still readable, slightly dimmer */
.wf-panel .window-button.minimized label {{
    color: {p['dim']};
    opacity: 1.0;
}}
.wf-panel .window-button.minimized.activated label {{
    color: {p['accent_fg']};
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
    { "backdrop-filter: blur(20px);" if "rgba" in p['surface'] or p['style'] == "glass" else "" }
{bevel if is_3d else f"    border: 1px solid {p['panel_border']};\\n    border-radius: {soft_radius};"}
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
    transition: background-color 0.2s ease, color 0.2s ease, border-color 0.2s ease;
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

/* ── Settings app (wf-settings) — full chrome, not just window {{}} ──────── */
window.wf-settings,
.wf-settings {{
    background-color: {p['surface']};
    color: {p['surface_fg']};
}}

.wf-settings .wf-settings-toolbar {{
    background-color: {p['title_bg']};
    background: {p['title_bg']};
    color: {p['title_fg']};
    border-bottom: 2px solid {p['panel_border']};
}}

.wf-settings .wf-settings-toolbar label {{
    color: {p['title_fg']};
    font-weight: bold;
}}

.wf-settings .wf-settings-sidebar,
.wf-settings .wf-settings-sidebar > scrolledwindow,
.wf-settings .wf-settings-sidebar > scrolledwindow > viewport,
.wf-settings .wf-settings-sidebar > scrolledwindow > undershoot,
.wf-settings .wf-settings-sidebar > scrolledwindow > overshoot {{
    background-color: {p['title_bg']};
    background: {p['title_bg']};
    color: {p['title_fg']};
    border: none;
    box-shadow: none;
}}

.wf-settings .wf-settings-sidebar {{
    border-right: 2px solid {p['panel_border']};
}}

.wf-settings list.navigation-sidebar,
.wf-settings .wf-settings-sidebar list,
.wf-settings .wf-settings-sidebar list.navigation-sidebar {{
    background-color: transparent;
    background: transparent;
    color: {p['title_fg']};
    border: none;
    box-shadow: none;
}}

.wf-settings list.navigation-sidebar > row,
.wf-settings .wf-settings-sidebar list > row {{
    background-color: transparent;
    background: transparent;
    color: {p['title_fg']};
    border-radius: 0;
    margin: 0;
    padding: 0;
    border: none;
    outline: none;
    box-shadow: none;
}}

.wf-settings list.navigation-sidebar > row > *,
.wf-settings .wf-settings-sidebar list > row > * {{
    background: transparent;
}}

.wf-settings list.navigation-sidebar > row label,
.wf-settings .wf-settings-sidebar list > row label {{
    color: {p['title_fg']};
}}

.wf-settings list.navigation-sidebar > row:hover,
.wf-settings .wf-settings-sidebar list > row:hover {{
    background-color: {p['accent']};
    background: {p['accent']};
    color: {p['accent_fg']};
}}

.wf-settings list.navigation-sidebar > row:hover label,
.wf-settings .wf-settings-sidebar list > row:hover label {{
    color: {p['accent_fg']};
}}

.wf-settings list.navigation-sidebar > row:selected,
.wf-settings list.navigation-sidebar > row:selected:hover,
.wf-settings list.navigation-sidebar > row:selected:focus,
.wf-settings .wf-settings-sidebar list > row:selected,
.wf-settings .wf-settings-sidebar list > row:selected:hover {{
    background-color: {p['accent']};
    background: {p['accent']};
    color: {p['accent_fg']};
    outline: none;
    box-shadow: none;
}}

.wf-settings list.navigation-sidebar > row:selected label,
.wf-settings list.navigation-sidebar > row:selected:hover label,
.wf-settings .wf-settings-sidebar list > row:selected label {{
    color: {p['accent_fg']};
    font-weight: bold;
}}

.wf-settings .wf-settings-sidebar searchentry,
.wf-settings .wf-settings-sidebar entry {{
    background-color: {p['field_bg']};
    color: {p['field_fg']};
    border: 1px solid {p['panel_border']};
    border-radius: 0;
    margin: 4px 8px;
}}

.wf-settings .wf-settings-content,
.wf-settings .wf-settings-content > *,
.wf-settings .wf-settings-content scrolledwindow,
.wf-settings .wf-settings-content scrolledwindow > viewport {{
    background-color: {p['surface']};
    background: {p['surface']};
    color: {p['surface_fg']};
}}

.wf-settings .dim-label {{
    color: {p['dim']};
    opacity: 1;
}}

.wf-settings frame {{
    border: 1px solid {p['panel_border']};
    border-radius: 0;
    background-color: {p['surface_alt']};
    color: {p['surface_fg']};
}}

.wf-settings .wf-settings-status {{
    background-color: {p['title_bg']};
    color: {p['title_fg']};
    border-top: 1px solid {p['panel_border']};
}}
"""


# Theme-specific flourishes appended after the shared template.
EXTRAS: dict[str, str] = {
    "crt-phosphor": r"""
/* ==========================================================================
 * CRT Phosphor — Borg Collective Cybernetic Terminal Flourishes
 * Void chassis · green scanline grids · conduit piping · cybernetic nodes
 * ========================================================================== */

/* Panel: deeper void, scanlines, double conduit rail */
.wf-panel {
    background-color: rgba(2, 6, 4, 0.97);
    background-image:
        repeating-linear-gradient(
            0deg,
            transparent 0px,
            transparent 1px,
            rgba(57, 255, 20, 0.04) 1px,
            rgba(57, 255, 20, 0.04) 2px
        ),
        linear-gradient(180deg,
            rgba(57, 255, 20, 0.08) 0px,
            transparent 2px,
            transparent calc(100% - 3px),
            rgba(0, 229, 168, 0.1) 100%);
    border-top: 1px solid #1a8f2a;
    border-bottom: 2px solid #39ff14;
    box-shadow:
        0 0 16px rgba(57, 255, 20, 0.3),
        inset 0 1px 0 rgba(57, 255, 20, 0.2),
        inset 0 -2px 8px rgba(0, 0, 0, 0.75);
}

/* Phosphor bloom on bar text (scanline terminal feel) — not window titles */
.wf-panel .clock,
.wf-panel .battery,
.wf-panel .network,
.wf-panel .volume,
.wf-panel .menu,
.wf-panel .app-button,
.wf-panel .tray,
.wf-panel .clock label,
.wf-panel .battery label {
    color: #39ff14;
    font-family: monospace;
    letter-spacing: 0.06em;
    text-shadow:
        0 0 4px rgba(57, 255, 20, 0.75),
        0 0 12px rgba(57, 255, 20, 0.3);
}

.wf-panel .clock:hover,
.wf-panel .battery:hover,
.wf-panel .network:hover,
.wf-panel .volume:hover,
.wf-panel .menu:hover,
.wf-panel .app-button:hover,
.wf-panel .volume.selected,
.wf-panel .network.selected,
.wf-panel .menu.selected {
    color: #020804;
    background-color: #39ff14;
    border: 1px solid #00e5a8;
    text-shadow: none;
    box-shadow:
        0 0 16px rgba(57, 255, 20, 0.7),
        inset 0 0 8px rgba(0, 229, 168, 0.4);
}

/*
 * Window list: never keep green text on green plate.
 * Idle titles stay phosphor; hover/activated invert to void-on-phosphor.
 */
.wf-panel .window-list button,
.wf-panel .window-button,
.wf-panel .window-list button label,
.wf-panel .window-button label {
    color: #39ff14;
    font-family: monospace;
    letter-spacing: 0.04em;
    text-shadow: 0 0 4px rgba(57, 255, 20, 0.4);
    background-color: transparent;
}

.wf-panel .window-list button:hover,
.wf-panel .window-button:hover,
.wf-panel .window-list button.active,
.wf-panel .window-button.activated {
    color: #020804;
    background-color: #39ff14;
    border: 1px solid #00e5a8;
    text-shadow: none;
    box-shadow:
        0 0 16px rgba(57, 255, 20, 0.7),
        inset 0 0 8px rgba(0, 229, 168, 0.4);
}

.wf-panel .window-list button:hover label,
.wf-panel .window-button:hover label,
.wf-panel .window-list button.active label,
.wf-panel .window-button.activated label,
.wf-panel .window-list button.activated label {
    color: #020804;
    background-color: transparent;
    text-shadow: none;
    opacity: 1.0;
}

.wf-panel .window-list button:hover image,
.wf-panel .window-button:hover image,
.wf-panel .window-list button.active image,
.wf-panel .window-button.activated image {
    color: #020804;
    -gtk-icon-shadow:
        0 0 3px #39ff14,
        1px 1px 0 #00e5a8,
        -1px -1px 0 #00e5a8;
}

/* Node wells — hexagonal-ish hard frames (angular) */
.wf-panel .volume,
.wf-panel .network,
.wf-panel .battery,
.wf-panel .menu,
.wf-panel .tray-button {
    background-color: rgba(2, 10, 6, 0.9);
    border: 1px solid #1a8f2a;
    border-left: 3px solid #39ff14;
    border-radius: 0;
    box-shadow:
        inset 0 0 10px rgba(0, 45, 18, 0.75),
        0 0 6px rgba(57, 255, 20, 0.15);
}

.wf-panel .volume:hover,
.wf-panel .network:hover,
.wf-panel .battery:hover,
.wf-panel .menu:hover,
.wf-panel .tray-button:hover {
    background-color: #39ff14;
    border-color: #00e5a8;
    box-shadow: 0 0 16px rgba(57, 255, 20, 0.6);
}

/* Icons: brighter phosphor core + black cutout for readability on void */
.wf-panel .widget-icon,
.wf-panel .volume image,
.wf-panel .network image,
.wf-panel .battery image,
.wf-panel .menu image,
.wf-panel .tray-button image,
.wf-panel image.widget-icon {
    color: #39ff14;
    -gtk-icon-shadow:
        0 0 4px rgba(57, 255, 20, 0.8),
        0 1px 0 #020804,
        0 -1px 0 #020804,
        1px 0 0 #020804,
        -1px 0 0 #020804;
}

.wf-panel .volume:hover image,
.wf-panel .network:hover image,
.wf-panel .menu:hover image,
.wf-panel .battery:hover image,
.wf-panel .volume.selected image,
.wf-panel .network.selected image,
.wf-panel .menu.selected image {
    color: #020804;
    -gtk-icon-shadow:
        0 0 3px #39ff14,
        1px 1px 0 #00e5a8,
        -1px -1px 0 #00e5a8;
}

/* Launchers: green halo around app glyphs (drones / modules) */
.wf-panel image.launcher,
.wf-panel .launcher image,
.wf-panel .launchers image {
    filter:
        drop-shadow(0 0 1px #39ff14)
        drop-shadow(0 0 4px rgba(57, 255, 20, 0.65))
        drop-shadow(1px 1px 0 #020804);
}

.wf-panel .launcher-button,
.wf-panel .launcher {
    background-color: rgba(2, 10, 6, 0.85);
    border: 1px solid #1a8f2a;
    border-radius: 0;
    box-shadow: inset 0 0 8px rgba(0, 35, 15, 0.85);
}

.wf-panel .launcher-button:hover,
.wf-panel .launcher:hover {
    background-color: rgba(8, 28, 14, 0.95);
    border-color: #39ff14;
    box-shadow: 0 0 12px rgba(57, 255, 20, 0.45);
}

/* Popovers: collective console chassis with CRT scanlines and conduit border */
popover > contents,
popovercontents,
.popover > contents,
popovermenu contents,
popovermenu > contents {
    background-color: rgba(2, 7, 4, 0.98);
    background-image:
        repeating-linear-gradient(
            0deg,
            transparent 0px,
            transparent 1px,
            rgba(57, 255, 20, 0.04) 1px,
            rgba(57, 255, 20, 0.04) 2px
        ),
        linear-gradient(180deg, rgba(57, 255, 20, 0.08) 0px, transparent 4px);
    border: 1px solid #1a8f2a;
    border-top: 3px solid #39ff14;
    border-left: 3px solid #00e5a8;
    border-radius: 0;
    color: #7dff7a;
    box-shadow:
        0 0 24px rgba(57, 255, 20, 0.35),
        4px 4px 0 rgba(0, 0, 0, 0.85),
        inset 0 0 32px rgba(0, 24, 10, 0.7);
}

/* Sound Settings title — assimilation header strip */
.volume-popover-header {
    background-color: #020804;
    background-image:
        linear-gradient(90deg,
            rgba(57, 255, 20, 0.25) 0%,
            transparent 50%,
            rgba(0, 229, 168, 0.15) 100%);
    border: 1px solid #1a8f2a;
    border-left: 4px solid #39ff14;
    border-radius: 0;
    box-shadow: inset 0 0 12px rgba(57, 255, 20, 0.18);
}

.volume-popover-header label,
.volume-popover-title,
.volume-popover-title * {
    color: #39ff14;
    font-family: monospace;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    text-shadow:
        0 0 6px rgba(57, 255, 20, 0.75),
        0 0 16px rgba(57, 255, 20, 0.4);
}

.volume-popover-section {
    background-color: rgba(4, 12, 8, 0.95);
    border: 1px solid #1a3d22;
    border-left: 3px solid #00e5a8;
    border-radius: 0;
    box-shadow: inset 0 0 12px rgba(0, 0, 0, 0.6);
}

.volume-popover-section-title {
    color: #00e5a8;
    font-family: monospace;
    letter-spacing: 0.2em;
    text-shadow: 0 0 5px rgba(0, 229, 168, 0.5);
}

.volume-popover-pct,
.volume-popover-voss-title,
.volume-popover-voss-lbl,
.volume-popover-voss-fmt,
.dim-label {
    font-family: monospace;
    color: #7dff7a;
    text-shadow: 0 0 4px rgba(57, 255, 20, 0.3);
}

.volume-popover-meter {
    background-color: #010402;
    background-image:
        repeating-linear-gradient(
            90deg,
            transparent 0px,
            transparent 2px,
            rgba(0, 20, 5, 0.35) 2px,
            rgba(0, 20, 5, 0.35) 4px
        );
    border: 1px solid #1a8f2a;
    border-radius: 0;
    box-shadow:
        inset 0 0 12px rgba(0, 0, 0, 0.9),
        0 0 8px rgba(57, 255, 20, 0.15);
}

.volume-popover-scale trough {
    background-color: #010805;
    border: 1px solid #1a3d22;
    border-radius: 0;
}

.volume-popover-scale highlight {
    background-color: #39ff14;
    background-image:
        linear-gradient(90deg, #1a8f2a, #39ff14);
    box-shadow: 0 0 10px rgba(57, 255, 20, 0.65);
    border-radius: 0;
}

.volume-popover-section:nth-child(5) .volume-popover-scale highlight {
    background-color: #00e5a8;
    background-image:
        linear-gradient(90deg, #008080, #00e5a8);
    box-shadow: 0 0 10px rgba(0, 229, 168, 0.6);
}

.volume-popover-scale slider {
    background-color: #041208;
    border: 1px solid #39ff14;
    border-radius: 0;
    min-width: 14px;
    min-height: 14px;
    box-shadow:
        0 0 8px rgba(57, 255, 20, 0.65),
        inset 0 0 4px rgba(57, 255, 20, 0.5);
}

/* Network: conduit list / active drone highlight */
.network-control-center .device,
.network-control-center .freebsd-iface {
    background-color: rgba(4, 12, 8, 0.95);
    border: 1px solid #1a3d22;
    border-left: 3px solid #1a8f2a;
    border-radius: 0;
    box-shadow: inset 0 0 10px rgba(0, 0, 0, 0.5);
}

.network-control-center .freebsd-iface.is-up {
    border-left-color: #39ff14;
    box-shadow:
        inset 0 0 10px rgba(0, 0, 0, 0.5),
        0 0 10px rgba(57, 255, 20, 0.15);
}

.network-control-center .access-point:hover,
.network-control-center .access-point.connected,
.network-control-center .access-point.active {
    background-color: rgba(8, 28, 14, 0.95);
    border: 1px solid #39ff14;
    border-left: 4px solid #00e5a8;
    color: #39ff14;
    box-shadow: 0 0 12px rgba(57, 255, 20, 0.25);
}

.network-control-center .access-point:hover label,
.network-control-center .access-point.connected label {
    color: #39ff14;
    text-shadow: 0 0 5px rgba(57, 255, 20, 0.5);
    font-family: monospace;
}

.network-control-center .access-point.saved .ap-saved-badge {
    background-color: #00e5a8;
    color: #020804;
    border-radius: 0;
    letter-spacing: 0.1em;
    text-transform: uppercase;
}

/* Buttons: hard nodes */
.popover button,
.network-control-center button,
window button {
    background-color: rgba(4, 12, 8, 0.95);
    border: 1px solid #1a8f2a;
    border-radius: 0;
    color: #7dff7a;
    font-family: monospace;
    letter-spacing: 0.08em;
    text-shadow: 0 0 4px rgba(57, 255, 20, 0.4);
    box-shadow: inset 0 0 8px rgba(0, 0, 0, 0.6);
}

.popover button:hover,
.network-control-center button:hover,
window button:hover {
    background-color: #39ff14;
    border-color: #00e5a8;
    color: #020804;
    text-shadow: none;
    box-shadow: 0 0 16px rgba(57, 255, 20, 0.6);
}

.popover button.destructive-action,
.network-control-center button.destructive-action {
    background-color: rgba(40, 6, 6, 0.95);
    border: 1px solid #b23b3b;
    color: #ff8fa3;
    text-shadow: 0 0 4px rgba(255, 85, 85, 0.4);
}

.popover button.destructive-action:hover,
.network-control-center button.destructive-action:hover {
    background-color: #b23b3b;
    border-color: #ff5555;
    color: #ffffff;
    box-shadow: 0 0 16px rgba(178, 59, 59, 0.6);
}

/* Fields: dark wells with cyan-green caret line feel */
entry,
entry text,
passwordentry,
passwordentry text,
.volume-popover-combo,
.volume-popover-device-combo,
combobox,
combobox button {
    background-color: #010805;
    border: 1px solid #1a3d22;
    border-bottom: 2px solid #00e5a8;
    border-radius: 0;
    color: #7dff7a;
    font-family: monospace;
    caret-color: #39ff14;
}

entry:focus,
entry:focus text,
passwordentry:focus,
passwordentry:focus text {
    border-color: #39ff14;
    box-shadow: 0 0 10px rgba(57, 255, 20, 0.35);
    color: #39ff14;
}

/* Window chrome: cube header */
headerbar,
window headerbar,
.titlebar {
    background-color: #020804;
    background-image:
        linear-gradient(90deg, rgba(57, 255, 20, 0.25), transparent 50%);
    color: #39ff14;
    border-bottom: 2px solid #39ff14;
}

headerbar label,
headerbar .title,
window headerbar label,
window headerbar .title {
    color: #39ff14;
    font-family: monospace;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    text-shadow: 0 0 8px rgba(57, 255, 20, 0.65);
}

/* Separators as energy conduits */
.popover separator,
.volume-popover-root separator,
separator {
    background-color: #1a8f2a;
    min-height: 1px;
    margin: 4px 0;
    box-shadow: 0 0 6px rgba(57, 255, 20, 0.35);
}

/* Menu / list selection = “assimilated” row */
popover listbox row:hover,
popover listbox row:selected,
popover listview row:hover,
popover listview row:selected,
popovermenu modelbutton:hover,
popovermenu modelbutton:selected {
    background-color: rgba(12, 40, 20, 0.95);
    color: #39ff14;
    border-left: 4px solid #00e5a8;
    box-shadow: inset 0 0 12px rgba(57, 255, 20, 0.25);
}

/* Network quality: collective signal tiers */
.wf-panel .network.excellent image { color: #39ff14; }
.wf-panel .network.good image      { color: #00e5a8; }
.wf-panel .network.medium image    { color: #c8e000; }
.wf-panel .network.weak image      { color: #e08020; }
.wf-panel .network.none image      { color: #1a3d22; }
""",
}


def main() -> None:
    for tid, pal in THEMES.items():
        css = render(tid, pal)
        # Clean up accidental escaped newlines from f-string branch hacks
        css = css.replace("\\n", "\n")
        extra = EXTRAS.get(tid, "")
        if extra:
            css = css.rstrip() + "\n" + extra.lstrip("\n")
        path = OUT / f"{tid}.css"
        path.write_text(css)
        print(f"wrote {path.name} ({len(css.splitlines())} lines)")


if __name__ == "__main__":
    main()

