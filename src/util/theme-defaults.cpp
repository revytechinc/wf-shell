#include "theme-defaults.hpp"

#include <algorithm>
#include <map>

namespace wf_shell
{

std::string theme_id_from_css_path(const std::string& css_path)
{
    /* Trim */
    std::string path = css_path;
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t' ||
            path.back() == '\n' || path.back() == '\r'))
    {
        path.pop_back();
    }
    size_t i = 0;
    while (i < path.size() && (path[i] == ' ' || path[i] == '\t'))
    {
        ++i;
    }
    path = path.substr(i);

    if (path.empty())
    {
        return "default";
    }

    /* Strip directory */
    auto slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    /* Strip extension */
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
    {
        base = base.substr(0, dot);
    }
    if (base.empty() || base == "default")
    {
        return "default";
    }
    return base;
}

std::string theme_default_menu_icon_id(const std::string& theme_id)
{
    static const std::map<std::string, std::string> map = {
        {"default", "wayfire"},
        {"win95", "win95-start"},
        {"system7", "system7-apple"},
        {"amiga-workbench", "amiga-wb"},
        {"crt-phosphor", "crt-node"},
        {"synthwave", "synth-grid"},
        {"miami-cyberpunk", "neon-orb"},
        {"nord", "nord-circle"},
        {"dracula", "dracula-bat"},
        {"rose-pine", "rose-bloom"},
        {"tokyo-night", "tokyo-pulse"},
        {"catppuccin-mocha", "catppuccin-latte"},
    };
    auto it = map.find(theme_id);
    return it != map.end() ? it->second : "wayfire";
}

bool is_default_theme_id(const std::string& theme_id)
{
    return theme_id.empty() || theme_id == "default";
}

ThemeSelectionState select_theme(const std::string& theme_id,
    const std::string& themes_install_dir)
{
    ThemeSelectionState st;
    if (is_default_theme_id(theme_id))
    {
        st.theme_id      = "default";
        st.css_path      = {};
        st.menu_icon_id  = theme_default_menu_icon_id("default");
        return st;
    }

    st.theme_id     = theme_id;
    st.menu_icon_id = theme_default_menu_icon_id(theme_id);
    if (!themes_install_dir.empty())
    {
        st.css_path = themes_install_dir;
        if (st.css_path.back() != '/')
        {
            st.css_path.push_back('/');
        }
        st.css_path += theme_id + ".css";
    } else
    {
        /* Logical path used by tests when dir is not supplied */
        st.css_path = theme_id + ".css";
    }
    return st;
}

ThemeSelectionState clear_theme_to_defaults()
{
    return select_theme("default");
}

bool theme_defaults_are_restored(const ThemeSelectionState& state)
{
    return state.theme_id == "default" &&
           state.css_path.empty() &&
           state.menu_icon_id == "wayfire";
}

std::vector<std::string> menu_icon_logical_candidates(const std::string& theme_id)
{
    std::vector<std::string> out;
    const std::string pack = theme_default_menu_icon_id(theme_id);
    out.push_back(pack);
    if (pack != "wayfire")
    {
        out.push_back("wayfire");
    }
    const char *safe[] = {
        "view-app-grid-symbolic",
        "open-menu-symbolic",
        "applications-menu",
        "start-here",
        "application-x-executable",
    };
    for (auto *s : safe)
    {
        if (std::find(out.begin(), out.end(), s) == out.end())
        {
            out.push_back(s);
        }
    }
    return out;
}

std::vector<std::string> themed_menu_icon_theme_ids()
{
    return {
        "win95",
        "system7",
        "amiga-workbench",
        "crt-phosphor",
        "synthwave",
        "miami-cyberpunk",
        "nord",
        "dracula",
        "rose-pine",
        "tokyo-night",
        "catppuccin-mocha",
    };
}

} // namespace wf_shell
