#include "app.hpp"

#include "config-backend.hpp"
#include "settings-theme.hpp"
#include "startup-gate.hpp"

#include <wayfire/config/section.hpp>
#include <wayfire/config/xml.hpp>
#include <libxml/tree.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sys/stat.h>

namespace wf_settings
{
namespace
{

const std::vector<std::string> kCategoryOrder = {
    "General",
    "Accessibility",
    "Desktop",
    "Shell",
    "Effects",
    "Window Management",
    "Utility",
    "Other",
};

/* Sections covered by Common (curated) pages — never also list as raw plugins. */
const std::set<std::string> kCuratedShellSections = {
    "panel", "dock", "background",
};

/* Wayfire sections superseded by Display (discover + apply, not text mode=). */
bool is_display_owned_section(const std::string& name)
{
    if (name == "output")
    {
        return true;
    }
    /* Per-monitor sections written by Display apply: output:HDMI-A-1 */
    if (name.rfind("output:", 0) == 0)
    {
        return true;
    }
    return false;
}

std::string lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string xml_child_text(xmlNode *node, const char *name)
{
    if (!node)
    {
        return {};
    }
    for (xmlNode *ch = node->children; ch; ch = ch->next)
    {
        if (ch->type != XML_ELEMENT_NODE || !ch->name)
        {
            continue;
        }
        std::string n = reinterpret_cast<const char*>(ch->name);
        if (n == name || n == std::string("_") + name)
        {
            xmlChar *c = xmlNodeGetContent(ch);
            if (!c)
            {
                return {};
            }
            std::string s = reinterpret_cast<char*>(c);
            xmlFree(c);
            return s;
        }
    }
    return {};
}

std::string section_category(ConfigDomain dom, const std::string& section)
{
    if (dom == ConfigDomain::Shell)
    {
        return "Shell";
    }
    auto sec = ConfigBackend::instance().wayfire.get_section(section);
    if (!sec)
    {
        return "Other";
    }
    xmlNode *node = wf::config::xml::get_section_xml_node(sec);
    for (xmlNode *n = node; n; n = n->parent)
    {
        auto cat = xml_child_text(n, "category");
        if (!cat.empty())
        {
            return cat;
        }
    }
    return "Other";
}

std::string section_title(ConfigDomain dom, const std::string& section)
{
    auto& b = ConfigBackend::instance();
    auto& mgr = (dom == ConfigDomain::Wayfire) ? b.wayfire : b.shell;
    auto sec = mgr.get_section(section);
    if (sec)
    {
        xmlNode *node = wf::config::xml::get_section_xml_node(sec);
        auto shortn = xml_child_text(node, "short");
        if (!shortn.empty())
        {
            return shortn;
        }
    }
    return section;
}

std::string section_blurb(ConfigDomain dom, const std::string& section)
{
    auto& b = ConfigBackend::instance();
    auto& mgr = (dom == ConfigDomain::Wayfire) ? b.wayfire : b.shell;
    auto sec = mgr.get_section(section);
    if (!sec)
    {
        return {};
    }
    return xml_child_text(wf::config::xml::get_section_xml_node(sec), "long");
}

std::string build_search_blob(const NavPlugin& p)
{
    std::string blob = p.title + " " + p.section + " " + p.category + " " + p.blurb;
    auto& b = ConfigBackend::instance();
    auto& mgr = (p.domain == ConfigDomain::Wayfire) ? b.wayfire : b.shell;
    if (auto sec = mgr.get_section(p.section))
    {
        for (auto& opt : sec->get_registered_options())
        {
            if (!opt)
            {
                continue;
            }
            blob += " ";
            blob += opt->get_name();
            xmlNode *on = wf::config::xml::get_option_xml_node(opt);
            auto sh = xml_child_text(on, "short");
            if (!sh.empty())
            {
                blob += " ";
                blob += sh;
            }
        }
    }
    return lower(blob);
}

void clear_listbox(Gtk::ListBox *list)
{
    while (auto *row = list->get_row_at_index(0))
    {
        list->remove(*row);
    }
}

} // namespace

SettingsApp::SettingsApp() :
    /* NON_UNIQUE: avoid session-bus single-instance races when DBUS is flaky. */
    Gtk::Application("org.wayfire.wf-settings", Gio::Application::Flags::NON_UNIQUE)
{}

bool SettingsApp::compositor_still_alive() const
{
    /* TAOCP: re-check facts at the boundary; never trust an earlier gate alone. */
    return evaluate_startup_gate().safe_to_open();
}

void SettingsApp::on_activate()
{
    /*
     * Do NOT call ensure_session_env() here. That helper invents DISPLAY /
     * GDK_BACKEND / DBUS for panel *children*. Settings is a normal window
     * client; inventing session vars has crashed this NVIDIA/FreeBSD seat.
     * main() already ran prepare_settings_process_env().
     */
    if (!compositor_still_alive())
    {
        std::cerr << "wf-settings: " << evaluate_startup_gate().user_summary() << "\n";
        quit();
        return;
    }

    try
    {
        ConfigBackend::instance().reload();
    } catch (const std::exception& e)
    {
        std::cerr << "wf-settings: config load failed (continuing empty): " << e.what() << "\n";
    } catch (...)
    {
        std::cerr << "wf-settings: config load failed (continuing empty)\n";
    }

    /* Theme is cosmetic — never block open; fail-soft. */
    try
    {
        reload_app_theme();
    } catch (...)
    {
        std::cerr << "wf-settings: theme load skipped\n";
    }

    if (window)
    {
        if (compositor_still_alive())
        {
            window->present();
        }
        return;
    }

    try
    {
        build_ui();
    } catch (const std::exception& e)
    {
        std::cerr << "wf-settings: UI build failed: " << e.what() << "\n";
        quit();
        return;
    }

    if (!compositor_still_alive())
    {
        std::cerr << "wf-settings: desktop went away while opening — not showing window\n";
        quit();
        return;
    }

    window->present();

    if (!start_plugin.empty())
    {
        focus_plugin_section(start_plugin);
    }
}

void SettingsApp::build_ui()
{
    window = new Gtk::ApplicationWindow();
    add_window(*window);
    window->set_title("Settings");
    window->set_default_size(1080, 720);
    window->add_css_class("wf-settings");

    auto outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    window->set_child(*outer);

    /*
     * Apple HIG modeless prefs: no Save / Apply / Cancel chrome.
     * Changing a control commits. Status line is quiet unless something fails.
     */
    auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    top->add_css_class("wf-settings-toolbar");
    top->set_margin_start(12);
    top->set_margin_end(12);
    top->set_margin_top(10);
    top->set_margin_bottom(6);
    auto win_title = Gtk::make_managed<Gtk::Label>();
    win_title->set_markup("<b>Settings</b>");
    win_title->set_halign(Gtk::Align::START);
    win_title->set_hexpand(true);
    top->append(*win_title);
    outer->append(*top);

    auto body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    body->set_vexpand(true);
    outer->append(*body);

    /* Left nav: search + list of pages (curated + every plugin) */
    auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    left->add_css_class("wf-settings-sidebar");
    left->set_size_request(260, -1);
    left->set_hexpand(false);
    left->set_vexpand(true);

    search = Gtk::make_managed<Gtk::SearchEntry>();
    search->set_placeholder_text("Find a setting…");
    search->set_margin_start(8);
    search->set_margin_end(8);
    search->set_margin_top(10);
    search->set_margin_bottom(8);
    left->append(*search);

    auto side_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    side_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    side_scroll->set_vexpand(true);
    side_scroll->set_hexpand(true);
    side_scroll->set_has_frame(false);
    sidebar = Gtk::make_managed<Gtk::ListBox>();
    sidebar->set_selection_mode(Gtk::SelectionMode::SINGLE);
    sidebar->add_css_class("navigation-sidebar");
    sidebar->set_hexpand(true);
    sidebar->set_vexpand(true);
    side_scroll->set_child(*sidebar);
    left->append(*side_scroll);
    body->append(*left);

    auto right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    right->add_css_class("wf-settings-content");
    right->set_hexpand(true);
    right->set_vexpand(true);
    body->append(*right);

    stack = Gtk::make_managed<Gtk::Stack>();
    stack->set_hexpand(true);
    stack->set_vexpand(true);
    stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    right->append(*stack);

    status = Gtk::make_managed<Gtk::Label>();
    status->set_halign(Gtk::Align::START);
    status->set_margin(8);
    status->add_css_class("dim-label");
    status->add_css_class("wf-settings-status");
    right->append(*status);

    /*
     * Lazy curated pages (Factory Method): placeholders only until selected.
     * Building every page (Session power probes, etc.) on open was heavy and
     * unsafe. Display never probes until the user clicks “Find monitors”.
     */
    auto add_placeholder = [&] (const char *id, const char *title) {
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_hexpand(true);
        scroll->set_vexpand(true);
        auto hold = Gtk::make_managed<Gtk::Label>("Loading…");
        hold->set_margin(24);
        hold->set_halign(Gtk::Align::START);
        hold->add_css_class("dim-label");
        scroll->set_child(*hold);
        stack->add(*scroll, id, title);
    };

    add_placeholder("panel", "Panel");
    add_placeholder("desktop", "Desktop");
    add_placeholder("dock", "Dock");
    add_placeholder("display", "Displays");
    add_placeholder("sound", "Sound");
    add_placeholder("network", "Network");
    add_placeholder("session", "Power & logout");
    add_placeholder("mcp", "AI helpers");

    rebuild_catalog();
    rebuild_sidebar();

    search->signal_search_changed().connect([this] () { on_search_changed(); });

    sidebar->signal_row_selected().connect([this] (Gtk::ListBoxRow *row) {
        if (filling_sidebar || !row)
        {
            return;
        }
        auto id = row->get_name();
        if (id.empty() || id.rfind("hdr:", 0) == 0)
        {
            return;
        }
        select_page(id);
    });

    /* Open Panel first — everyday settings, no compositor probes. */
    for (int i = 0;; ++i)
    {
        auto *row = sidebar->get_row_at_index(i);
        if (!row)
        {
            break;
        }
        auto id = row->get_name();
        if (id == "panel")
        {
            sidebar->select_row(*row);
            select_page(id);
            break;
        }
    }
}

void SettingsApp::rebuild_catalog()
{
    catalog.clear();
    auto& b = ConfigBackend::instance();

    auto add = [&] (ConfigDomain dom, const std::string& name) {
        if (dom == ConfigDomain::Shell && kCuratedShellSections.count(name))
        {
            return; /* Panel / Dock / Desktop cover these */
        }
        if (dom == ConfigDomain::Wayfire && is_display_owned_section(name))
        {
            return; /* Display page owns monitors — no raw mode/position text */
        }
        NavPlugin p;
        p.domain   = dom;
        p.section  = name;
        p.title    = section_title(dom, name);
        p.category = section_category(dom, name);
        p.blurb    = section_blurb(dom, name);
        p.stack_id = PluginPage::stack_id(dom, name);
        p.search_blob = build_search_blob(p);
        catalog.push_back(std::move(p));
    };

    for (const auto& n : b.wayfire_section_names())
    {
        add(ConfigDomain::Wayfire, n);
    }
    for (const auto& n : b.shell_section_names())
    {
        add(ConfigDomain::Shell, n);
    }
}

void SettingsApp::rebuild_sidebar()
{
    filling_sidebar = true;
    clear_listbox(sidebar);
    sidebar_rows.clear();
    sidebar_headers.clear();

    auto add_header = [&] (const std::string& label) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_selectable(false);
        row->set_activatable(false);
        row->set_name("hdr:" + label);
        auto l = Gtk::make_managed<Gtk::Label>();
        l->set_markup("<span size=\"small\" weight=\"bold\" alpha=\"70%\">" +
            Glib::Markup::escape_text(label) + "</span>");
        l->set_halign(Gtk::Align::START);
        l->set_margin_start(12);
        l->set_margin_end(8);
        l->set_margin_top(10);
        l->set_margin_bottom(4);
        row->set_child(*l);
        sidebar->append(*row);
        sidebar_headers.push_back(row);
    };

    auto add_item = [&] (const std::string& id, const std::string& title,
                         const std::string& subtitle = {}) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(id);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        box->set_margin_start(12);
        box->set_margin_end(8);
        box->set_margin_top(6);
        box->set_margin_bottom(6);
        auto t = Gtk::make_managed<Gtk::Label>(title);
        t->set_halign(Gtk::Align::START);
        t->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*t);
        if (!subtitle.empty())
        {
            auto s = Gtk::make_managed<Gtk::Label>(subtitle);
            s->set_halign(Gtk::Align::START);
            s->add_css_class("dim-label");
            s->set_ellipsize(Pango::EllipsizeMode::END);
            box->append(*s);
        }
        row->set_child(*box);
        sidebar->append(*row);
        sidebar_rows[id] = row;
    };

    /* —— Curated settings grouped logically (Apple System Settings style) —— */
    add_header("Appearance");
    add_item("panel", "Panel (look & icons)");
    add_item("desktop", "Desktop & wallpaper");
    add_item("dock", "Dock");

    add_header("Hardware & Network");
    add_item("display", "Displays");
    add_item("sound", "Sound");
    add_item("network", "Network icon");

    add_header("System");
    add_item("session", "Power & logout");
    add_item("mcp", "AI helpers");

    /* —— Every plugin, grouped by category (same level as Common) —— */
    std::map<std::string, std::vector<const NavPlugin*>> by_cat;
    for (const auto& p : catalog)
    {
        by_cat[p.category].push_back(&p);
    }
    for (auto& kv : by_cat)
    {
        std::sort(kv.second.begin(), kv.second.end(),
            [] (const NavPlugin *a, const NavPlugin *b) {
                if (a->title != b->title)
                {
                    return a->title < b->title;
                }
                return a->section < b->section;
            });
    }

    auto emit_cat = [&] (const std::string& cat) {
        auto it = by_cat.find(cat);
        if (it == by_cat.end() || it->second.empty())
        {
            return;
        }
        add_header(cat);
        for (const auto *p : it->second)
        {
            add_item(p->stack_id, p->title, p->section);
        }
    };

    for (const auto& cat : kCategoryOrder)
    {
        emit_cat(cat);
    }
    for (const auto& kv : by_cat)
    {
        if (std::find(kCategoryOrder.begin(), kCategoryOrder.end(), kv.first) ==
            kCategoryOrder.end())
        {
            emit_cat(kv.first);
        }
    }

    filling_sidebar = false;
    apply_sidebar_filter();

    if (status)
    {
        status->set_text(std::to_string(catalog.size()) +
            " plugins in sidebar · Everyday tasks on top · search filters the list");
    }
}

void SettingsApp::on_search_changed()
{
    filter = search->get_text();
    apply_sidebar_filter();
}

void SettingsApp::apply_sidebar_filter()
{
    const std::string fl = lower(filter);
    const bool filtering = !fl.empty();

    /* Curated pages: match title/id */
    static const std::vector<std::pair<std::string, std::string>> curated = {
        {"display", "display displays monitor screen size resolution refresh smoothness"},
        {"panel", "panel theme layout menu bar widgets"},
        {"desktop", "desktop wallpaper workspace background picture"},
        {"dock", "dock"},
        {"sound", "sound volume audio"},
        {"network", "network wifi ethernet"},
        {"session", "session logout lock suspend power shutdown restart sleep"},
        {"mcp", "mcp ai model server"},
    };

    auto curated_visible = [&] (const std::string& id) -> bool {
        if (!filtering)
        {
            return true;
        }
        for (const auto& c : curated)
        {
            if (c.first == id)
            {
                return lower(c.second).find(fl) != std::string::npos ||
                    id.find(fl) != std::string::npos;
            }
        }
        return id.find(fl) != std::string::npos;
    };

    bool any_common = false;
    for (const auto& c : curated)
    {
        auto it = sidebar_rows.find(c.first);
        if (it == sidebar_rows.end())
        {
            continue;
        }
        bool vis = curated_visible(c.first);
        it->second->set_visible(vis);
        if (vis)
        {
            any_common = true;
        }
    }

    std::map<std::string, bool> cat_any;
    for (const auto& p : catalog)
    {
        auto it = sidebar_rows.find(p.stack_id);
        if (it == sidebar_rows.end())
        {
            continue;
        }
        bool vis = !filtering || p.search_blob.find(fl) != std::string::npos;
        it->second->set_visible(vis);
        if (vis)
        {
            cat_any[p.category] = true;
        }
    }

    for (auto *hdr : sidebar_headers)
    {
        auto name = hdr->get_name(); /* hdr:Everyday / hdr:Common or hdr:Effects */
        if (name == "hdr:Common" || name == "hdr:Everyday")
        {
            hdr->set_visible(any_common || !filtering);
            continue;
        }
        if (name.rfind("hdr:", 0) == 0)
        {
            std::string cat = name.substr(4);
            hdr->set_visible(!filtering || cat_any[cat]);
        }
    }
}

void SettingsApp::ensure_plugin_page(const NavPlugin& p)
{
    if (plugin_pages.count(p.stack_id))
    {
        return;
    }
    auto page = std::make_unique<PluginPage>(
        p.domain, p.section, p.title, p.blurb, p.category);
    page->set_status_target(status);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);
    scroll->set_child(*page);
    stack->add(*scroll, p.stack_id, p.title);
    plugin_pages[p.stack_id] = std::move(page);
}

void SettingsApp::ensure_curated_page(const std::string& id)
{
    if (!stack)
    {
        return;
    }

    auto replace_child = [&] (Gtk::Widget& page) {
        auto *scroll = dynamic_cast<Gtk::ScrolledWindow*>(stack->get_child_by_name(id));
        if (!scroll)
        {
            return;
        }
        scroll->set_child(page);
    };

    if (id == "panel" && !panel_page)
    {
        panel_page = std::make_unique<PanelPage>();
        panel_page->set_status_target(status);
        panel_page->set_theme_applied_callback([] () { reload_app_theme(); });
        replace_child(*panel_page);
    } else if (id == "desktop" && !desktop_page)
    {
        desktop_page = std::make_unique<DesktopPage>();
        desktop_page->set_status_target(status);
        replace_child(*desktop_page);
    } else if (id == "dock" && !dock_page)
    {
        dock_page = std::make_unique<DockPage>();
        dock_page->set_status_target(status);
        replace_child(*dock_page);
    } else if (id == "display" && !display_page)
    {
        display_page = std::make_unique<DisplayPage>();
        display_page->set_status_target(status);
        replace_child(*display_page);
    } else if (id == "sound" && !sound_page)
    {
        sound_page = std::make_unique<SoundPage>();
        sound_page->set_status_target(status);
        replace_child(*sound_page);
    } else if (id == "network" && !network_page)
    {
        network_page = std::make_unique<NetworkPage>();
        network_page->set_status_target(status);
        replace_child(*network_page);
    } else if (id == "session" && !session_page)
    {
        session_page = std::make_unique<SessionPage>();
        session_page->set_status_target(status);
        replace_child(*session_page);
    } else if (id == "mcp" && !mcp_page)
    {
        mcp_page = std::make_unique<McpPage>();
        mcp_page->set_status_target(status);
        replace_child(*mcp_page);
    }
}

void SettingsApp::select_page(const std::string& id)
{
    if (!stack)
    {
        return;
    }

    /* Plugin stack ids */
    if (id.rfind("w:", 0) == 0 || id.rfind("s:", 0) == 0)
    {
        const NavPlugin *found = nullptr;
        for (const auto& p : catalog)
        {
            if (p.stack_id == id)
            {
                found = &p;
                break;
            }
        }
        if (!found)
        {
            return;
        }
        ensure_plugin_page(*found);
        plugin_pages[id]->refresh();
        stack->set_visible_child(id);
        return;
    }

    ensure_curated_page(id);
    stack->set_visible_child(id);

    /*
     * Display: NEVER auto-call refresh() here.
     * refresh() runs wlr-randr against the live compositor and has crashed
     * Wayfire on open. User must click "Find monitors".
     *
     * Other pages: refresh only after lazy construct (config file reads only).
     */
    if (id == "panel" && panel_page)
    {
        panel_page->refresh();
    } else if (id == "desktop" && desktop_page)
    {
        desktop_page->refresh();
    } else if (id == "dock" && dock_page)
    {
        dock_page->refresh();
    } else if (id == "sound" && sound_page)
    {
        sound_page->refresh();
    } else if (id == "network" && network_page)
    {
        network_page->refresh();
    } else if (id == "session" && session_page)
    {
        session_page->refresh();
    } else if (id == "mcp" && mcp_page)
    {
        mcp_page->refresh();
    } else if (id == "display" && display_page)
    {
        Glib::signal_idle().connect_once([this] () {
            if (display_page)
            {
                display_page->refresh();
            }
        });
    }
}

void SettingsApp::focus_plugin_section(const std::string& section)
{
    for (const auto& p : catalog)
    {
        if (p.section == section)
        {
            if (search)
            {
                search->set_text("");
            }
            filter.clear();
            apply_sidebar_filter();
            ensure_plugin_page(p);
            if (auto it = sidebar_rows.find(p.stack_id); it != sidebar_rows.end())
            {
                sidebar->select_row(*it->second);
            }
            select_page(p.stack_id);
            return;
        }
    }
    /* curated aliases / superseded raw sections */
    if (section == "panel")
    {
        select_page("panel");
    } else if (section == "dock")
    {
        select_page("dock");
    } else if (section == "background")
    {
        select_page("desktop");
    } else if (is_display_owned_section(section))
    {
        select_page("display");
        if (auto it = sidebar_rows.find("display"); it != sidebar_rows.end())
        {
            sidebar->select_row(*it->second);
        }
    }
}

} // namespace wf_settings
