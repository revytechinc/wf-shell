#pragma once

#include <gtkmm.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config-backend.hpp"
#include "pages/display-page.hpp"
#include "pages/panel-page.hpp"
#include "pages/desktop-page.hpp"
#include "pages/session-page.hpp"
#include "pages/sound-page.hpp"
#include "pages/network-page.hpp"
#include "pages/dock-page.hpp"
#include "pages/mcp-page.hpp"
#include "pages/plugin-page.hpp"

namespace wf_settings
{

struct NavPlugin
{
    ConfigDomain domain = ConfigDomain::Wayfire;
    std::string section;
    std::string title;
    std::string category;
    std::string blurb;
    std::string stack_id; /* w:name or s:name */
    /** Search haystack: title + section + category + blurb + option names */
    std::string search_blob;
};

/**
 * Settings shell:
 *   sidebar = Common tasks + every plugin under its category (one level)
 *   content = one page at a time (curated task OR single plugin options)
 *   search  = filters the sidebar (not a dump list of options)
 */
class SettingsApp : public Gtk::Application
{
  public:
    SettingsApp();
    ~SettingsApp() override = default;

    void set_start_plugin(const std::string& name)
    {
        start_plugin = name;
    }

  protected:
    void on_activate() override;

  private:
    void build_ui();
    void rebuild_sidebar();
    void rebuild_catalog();
    void select_page(const std::string& id);
    void ensure_curated_page(const std::string& id);
    void ensure_plugin_page(const NavPlugin& p);
    void on_search_changed();
    void apply_sidebar_filter();
    void focus_plugin_section(const std::string& section);
    bool compositor_still_alive() const;

    Gtk::ApplicationWindow *window = nullptr;
    Gtk::SearchEntry *search = nullptr;
    Gtk::ListBox *sidebar = nullptr;
    Gtk::Stack *stack = nullptr;
    Gtk::Label *status = nullptr;

    std::unique_ptr<DisplayPage> display_page;
    std::unique_ptr<PanelPage> panel_page;
    std::unique_ptr<DesktopPage> desktop_page;
    std::unique_ptr<DockPage> dock_page;
    std::unique_ptr<SoundPage> sound_page;
    std::unique_ptr<NetworkPage> network_page;
    std::unique_ptr<SessionPage> session_page;
    std::unique_ptr<McpPage> mcp_page;

    std::vector<NavPlugin> catalog;
    std::map<std::string, std::unique_ptr<PluginPage>> plugin_pages;
    /** stack_id -> ListBoxRow for filter visibility */
    std::map<std::string, Gtk::ListBoxRow*> sidebar_rows;
    std::vector<Gtk::ListBoxRow*> sidebar_headers;

    std::string start_plugin;
    std::string filter;
    bool filling_sidebar = false;
};

} // namespace wf_settings
