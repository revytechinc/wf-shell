#include "settings-theme.hpp"

#include "shell-json-config.hpp"
#include "theme-catalog.hpp"
#include "gtk-utils.hpp"
#include "config-backend.hpp"
#include "apply-gate.hpp"

#include <gtkmm.h>
#include <iostream>
#include <vector>

#ifndef RESOURCEDIR
#define RESOURCEDIR "/usr/local/share/wf-shell"
#endif

namespace wf_settings
{
namespace
{

std::vector<Glib::RefPtr<Gtk::CssProvider>> g_providers;

std::string resolve_panel_css_path()
{
    /* JSON overrides INI */
    wf_shell::ShellJsonConfig jcfg;
    std::string jerr;
    if (wf_shell::load_shell_json_config(wf_shell::shell_json_config_path(), jcfg, &jerr))
    {
        auto sit = jcfg.sections.find("panel");
        if (sit != jcfg.sections.end())
        {
            auto cit = sit->second.find("css_path");
            if (cit != sit->second.end() && !cit->second.empty())
            {
                return cit->second;
            }
        }
    }

    auto ini = ConfigBackend::instance().shell_ini;
    if (ini.empty())
    {
        ini = default_shell_ini();
    }
    return wf_shell::get_ini_css_path(ini);
}

void add_file(const std::string& path, int priority)
{
    if (path.empty())
    {
        return;
    }
    auto css = load_css_from_path(path);
    if (!css)
    {
        std::cerr << "wf-settings: failed to load CSS " << path << "\n";
        return;
    }
    auto display = Gdk::Display::get_default();
    if (!display)
    {
        return;
    }
    Gtk::StyleContext::add_provider_for_display(display, css, priority);
    g_providers.push_back(css);
}

} // namespace

void reload_app_theme()
{
    auto display = Gdk::Display::get_default();
    if (!display)
    {
        return;
    }
    for (auto& p : g_providers)
    {
        Gtk::StyleContext::remove_provider_for_display(display, p);
    }
    g_providers.clear();

    /* Base shell chrome (same as panel) */
    add_file(std::string(RESOURCEDIR) + "/css/default.css",
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    const auto custom = resolve_panel_css_path();
    if (!custom.empty())
    {
        auto gate = wf_shell::validate_theme_css_path(custom);
        if (gate.ok)
        {
            add_file(custom, GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        else
        {
            std::cerr << "wf-settings: custom theme validation failed: " << gate.summary() << "\n";
        }
    }
}

} // namespace wf_settings
