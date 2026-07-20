#pragma once

/**
 * Human-facing labels for raw config values.
 * Settings UI should never show fill_and_crop / systemctl poweroff to end users
 * as the primary control language.
 *
 * Widget catalog: only present ids this build can construct
 * (see panel-capabilities.hpp). Never list weather if HAVE_WEATHER is off.
 */

#include "panel-capabilities.hpp"

#include <string>
#include <utility>
#include <vector>

namespace wf_settings
{
namespace ux
{

/** Wallpaper fill_mode → plain English */
inline std::string fill_mode_label(const std::string& raw)
{
    if (raw == "fill_and_crop")
    {
        return "Fill screen (crop edges)";
    }
    if (raw == "stretch")
    {
        return "Stretch to fit";
    }
    if (raw == "preserve_aspect")
    {
        return "Fit whole image (letterbox)";
    }
    if (raw == "centered")
    {
        return "Center (no scale)";
    }
    return raw.empty() ? "Fill screen (crop edges)" : raw;
}

inline const std::vector<std::pair<std::string, std::string>>& fill_mode_choices()
{
    static const std::vector<std::pair<std::string, std::string>> k = {
        {"fill_and_crop", "Fill screen (crop edges)"},
        {"stretch", "Stretch to fit"},
        {"preserve_aspect", "Fit whole image (letterbox)"},
        {"centered", "Center (no scale)"},
    };
    return k;
}

/** Panel edge position */
inline std::string panel_position_label(const std::string& raw)
{
    if (raw == "top")
    {
        return "Top of screen";
    }
    if (raw == "bottom")
    {
        return "Bottom of screen";
    }
    if (raw == "left")
    {
        return "Left side";
    }
    if (raw == "right")
    {
        return "Right side";
    }
    return raw;
}

inline const std::vector<std::pair<std::string, std::string>>& panel_position_choices()
{
    static const std::vector<std::pair<std::string, std::string>> k = {
        {"top", "Top of screen"},
        {"bottom", "Bottom of screen"},
        {"left", "Left side"},
        {"right", "Right side"},
    };
    return k;
}

/** Known panel widget id → friendly name (full static list — filter with available). */
struct WidgetInfo
{
    std::string id;
    std::string label;
    std::string blurb;
};

/** Full catalog of names; Settings must filter with panel_widget_available(). */
inline const std::vector<WidgetInfo>& panel_widget_catalog_all()
{
    static const std::vector<WidgetInfo> k = {
        {"menu", "Applications menu", "Start menu / app launcher"},
        {"launchers", "Pinned apps", "Shortcuts you pin on the bar"},
        {"window-list", "Open windows", "Buttons for running apps"},
        {"workspace-switcher", "Workspaces", "Switch virtual desktops"},
        {"volume", "Volume", "Speaker / headphone volume"},
        {"mixer", "Audio mixer", "Per-app volume"},
        {"network", "Network", "Wi‑Fi / Ethernet status"},
        {"battery", "Battery", "Laptop charge"},
        {"clock", "Clock", "Time and calendar"},
        {"tray", "System tray", "Background app icons"},
        {"notifications", "Notifications", "Notification center"},
        {"brightness", "Brightness", "Screen brightness"},
        {"language", "Keyboard layout", "Input language"},
        {"weather", "Weather", "Weather"},
        {"command-output", "Custom readout", "Output of a shell command"},
        {"spacing4", "Small gap", "Spacer between widgets"},
        {"spacing8", "Medium gap", "Larger spacer"},
        {"separator", "Separator", "Visual divider"},
    };
    return k;
}

/**
 * Only widgets this build can construct. Never present weather/volume/etc.
 * when the panel was built without them.
 */
inline std::vector<WidgetInfo> panel_widget_catalog()
{
    std::vector<WidgetInfo> out;
    for (const auto& w : panel_widget_catalog_all())
    {
        if (wf_shell::panel_widget_available(w.id))
        {
            out.push_back(w);
        }
    }
    return out;
}

inline std::string widget_label(const std::string& id)
{
    for (const auto& w : panel_widget_catalog_all())
    {
        if (w.id == id)
        {
            return w.label;
        }
    }
    if (id.rfind("spacing", 0) == 0)
    {
        return "Gap (" + id + ")";
    }
    return id;
}

inline std::string widget_blurb(const std::string& id)
{
    for (const auto& w : panel_widget_catalog_all())
    {
        if (w.id == id)
        {
            return w.blurb;
        }
    }
    return {};
}

/** Title-case snake/kebab for leftover raw keys shown as last resort. */
inline std::string humanize_token(std::string s)
{
    for (char& c : s)
    {
        if (c == '_' || c == '-')
        {
            c = ' ';
        }
    }
    bool cap = true;
    for (char& c : s)
    {
        if (cap && c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
        cap = (c == ' ');
    }
    return s;
}

} // namespace ux
} // namespace wf_settings
