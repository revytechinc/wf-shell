#include "panel-capabilities.hpp"
#include "theme-defaults.hpp"
#include "apply-gate.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace wf_shell
{
namespace
{

bool is_spacing_or_sep(const std::string& name)
{
    if (name.rfind("spacing", 0) == 0)
    {
        return name.size() > 7; /* spacingN */
    }
    if (name.rfind("separator", 0) == 0)
    {
        return true;
    }
    return false;
}

} // namespace

std::vector<std::string> available_panel_widget_ids()
{
    /* Keep in lockstep with panel.cpp::widget_from_name feature #ifdefs. */
    std::vector<std::string> ids = {
        "menu",
        "launchers",
        "clock",
        "network",
        "battery",
        "window-list",
        "notifications",
        "tray",
        "command-output",
        "language",
        "workspace-switcher",
        "brightness",
        "spacing4",
        "spacing8",
        "separator",
    };

#ifdef HAVE_WEATHER
    ids.push_back("weather");
#endif
#ifdef HAVE_PULSE
    ids.push_back("volume");
#endif
#ifdef HAVE_WIREPLUMBER
    ids.push_back("mixer");
#endif

    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool panel_widget_available(const std::string& name)
{
    if (name.empty() || name == "none")
    {
        return true; /* none = empty zone, not a widget */
    }
    if (is_spacing_or_sep(name))
    {
        return true;
    }
    const auto avail = available_panel_widget_ids();
    return std::find(avail.begin(), avail.end(), name) != avail.end();
}

std::string sanitize_panel_widgets_list(const std::string& list)
{
    std::istringstream in(list);
    std::string tok;
    std::vector<std::string> kept;
    std::vector<std::string> dropped;
    while (in >> tok)
    {
        if (tok == "none" || tok.empty())
        {
            continue;
        }
        if (panel_widget_available(tok))
        {
            kept.push_back(tok);
        } else
        {
            dropped.push_back(tok);
        }
    }
    if (!dropped.empty())
    {
        std::ostringstream d;
        for (size_t i = 0; i < dropped.size(); ++i)
        {
            if (i)
            {
                d << ' ';
            }
            d << dropped[i];
        }
        gate_log("sanitize_widgets", "dropped unavailable: " + d.str());
    }
    if (kept.empty())
    {
        return "none";
    }
    std::ostringstream o;
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (i)
        {
            o << ' ';
        }
        o << kept[i];
    }
    return o.str();
}

std::string resolve_theme_menu_icon_path(const std::string& menu_icon_id,
    const std::string& resource_icons_dir,
    const std::string& user_menu_icons_dir)
{
    if (menu_icon_id.empty())
    {
        return {};
    }
    std::error_code ec;
    namespace fs = std::filesystem;

    /* Only search caller-provided roots first — discovery uses system path
     * explicitly. Do not silently invent icons from unrelated trees. */
    if (menu_icon_id == "wayfire")
    {
        const std::string candidates[] = {
            resource_icons_dir + "/wayfire.png",
            resource_icons_dir + "/wayfire.svg",
            resource_icons_dir + "/scalable/wayfire.svg",
        };
        for (const auto& p : candidates)
        {
            if (!resource_icons_dir.empty() && fs::is_regular_file(p, ec))
            {
                return p;
            }
        }
        return {};
    }

    const std::string dirs[] = {
        resource_icons_dir.empty() ? std::string{} : resource_icons_dir + "/menu",
        user_menu_icons_dir,
    };
    for (const auto& dir : dirs)
    {
        if (dir.empty())
        {
            continue;
        }
        for (const char *ext : {".svg", ".png", ".svgz"})
        {
            std::string p = dir + "/" + menu_icon_id + ext;
            if (fs::is_regular_file(p, ec))
            {
                return p;
            }
        }
    }
    return {};
}

bool theme_artifacts_complete(const std::string& theme_id,
    const std::string& css_path,
    const std::string& resource_icons_dir,
    const std::string& user_menu_icons_dir)
{
    if (is_default_theme_id(theme_id))
    {
        auto icon = resolve_theme_menu_icon_path("wayfire", resource_icons_dir,
            user_menu_icons_dir);
        const bool ok = !icon.empty();
        if (!ok)
        {
            gate_log("theme_artifacts", "default theme missing wayfire icon under " +
                resource_icons_dir);
        }
        return ok;
    }

    std::error_code ec;
    if (css_path.empty() || !std::filesystem::is_regular_file(css_path, ec) || ec)
    {
        gate_log("theme_artifacts", "missing CSS for " + theme_id + " path=" + css_path);
        return false;
    }

    const auto icon_id = theme_default_menu_icon_id(theme_id);
    auto icon = resolve_theme_menu_icon_path(icon_id, resource_icons_dir,
        user_menu_icons_dir);
    if (icon.empty())
    {
        gate_log("theme_artifacts", "REFUSE present theme " + theme_id +
            " — missing menu icon id=" + icon_id);
        return false;
    }
    gate_log("theme_artifacts", "complete " + theme_id + " css=" + css_path +
        " icon=" + icon);
    return true;
}

} // namespace wf_shell
