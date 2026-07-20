#include "panel-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"
#include "theme-defaults.hpp"
#include "ux-labels.hpp"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace wf_settings
{
namespace
{

std::vector<std::string> split_ws(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == ' ' || c == '\t' || c == ',')
        {
            if (!cur.empty() && cur != "none")
            {
                out.push_back(cur);
            }
            cur.clear();
        } else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty() && cur != "none")
    {
        out.push_back(cur);
    }
    return out;
}

std::string join_ws(const std::vector<std::string>& parts)
{
    if (parts.empty())
    {
        return "none";
    }
    std::ostringstream o;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
        {
            o << ' ';
        }
        o << parts[i];
    }
    return o.str();
}

} // namespace

PanelPage::PanelPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 12)
{
    set_margin(16);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Panel</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "Look and what’s on the bar. Pick widgets by name — no typing lists.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_column_spacing(12);
    grid->set_row_spacing(8);
    int row = 0;

    auto add_label = [&] (const char *text) {
        auto l = Gtk::make_managed<Gtk::Label>(text);
        l->set_halign(Gtk::Align::START);
        grid->attach(*l, 0, row, 1, 1);
    };

    add_label("Look");
    theme_drop = Gtk::make_managed<Gtk::DropDown>();
    theme_drop->set_hexpand(true);
    grid->attach(*theme_drop, 1, row++, 1, 1);

    add_label("Where");
    position_drop = Gtk::make_managed<Gtk::DropDown>();
    {
        std::vector<Glib::ustring> labels;
        for (const auto& p : ux::panel_position_choices())
        {
            position_values.push_back(p.first);
            labels.push_back(p.second);
        }
        position_drop->set_model(Gtk::StringList::create(labels));
    }
    grid->attach(*position_drop, 1, row++, 1, 1);

    add_label("Thickness");
    height_spin = Gtk::make_managed<Gtk::SpinButton>();
    height_spin->set_range(16, 128);
    height_spin->set_increments(1, 4);
    height_spin->set_digits(0);
    height_spin->set_tooltip_text("How tall (or wide) the bar is, in pixels");
    grid->attach(*height_spin, 1, row++, 1, 1);

    autohide_chk = Gtk::make_managed<Gtk::CheckButton>("Hide the bar until I move the pointer to the edge");
    grid->attach(*autohide_chk, 0, row++, 2, 1);

    menu_list_chk = Gtk::make_managed<Gtk::CheckButton>("Show the app menu as a list");
    grid->attach(*menu_list_chk, 0, row++, 2, 1);

    menu_cats_chk = Gtk::make_managed<Gtk::CheckButton>("Group apps by category in the menu");
    grid->attach(*menu_cats_chk, 0, row++, 2, 1);

    append(*grid);

    auto wtitle = Gtk::make_managed<Gtk::Label>();
    wtitle->set_markup("<b>What’s on the bar</b>");
    wtitle->set_halign(Gtk::Align::START);
    wtitle->set_margin_top(8);
    append(*wtitle);

    auto whint = Gtk::make_managed<Gtk::Label>(
        "Tick items for the left, middle, and right of the panel.");
    whint->set_wrap(true);
    whint->add_css_class("dim-label");
    whint->set_halign(Gtk::Align::START);
    append(*whint);

    widget_cols = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    widget_cols->set_homogeneous(true);
    append(*widget_cols);
    rebuild_widget_checks();

    info_lbl = Gtk::make_managed<Gtk::Label>();
    info_lbl->set_halign(Gtk::Align::START);
    info_lbl->set_wrap(true);
    info_lbl->add_css_class("dim-label");
    append(*info_lbl);

    auto actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    refresh_btn = Gtk::make_managed<Gtk::Button>("Reload");
    apply_btn = Gtk::make_managed<Gtk::Button>("Apply");
    apply_btn->add_css_class("suggested-action");
    actions->append(*refresh_btn);
    actions->append(*apply_btn);
    append(*actions);

    refresh_btn->signal_clicked().connect([this] () { refresh(); });
    apply_btn->signal_clicked().connect([this] () { on_apply(); });
    theme_drop->property_selected().signal_changed().connect([this] () {
        if (filling)
        {
            return;
        }
        auto idx = theme_drop->get_selected();
        if (idx < themes.size())
        {
            info_lbl->set_text(themes[idx].path.empty() ?
                "Default look" : ("Theme: " + themes[idx].name));
        }
    });

    refresh();
}

void PanelPage::rebuild_widget_checks()
{
    auto kids = widget_cols->get_children();
    for (auto *c : kids)
    {
        widget_cols->remove(*c);
    }
    widget_checks.clear();

    const char *zones[] = {"left", "center", "right"};
    const char *zone_titles[] = {"Left", "Middle", "Right"};

    for (int z = 0; z < 3; ++z)
    {
        auto frame = Gtk::make_managed<Gtk::Frame>();
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        col->set_margin(8);
        auto ht = Gtk::make_managed<Gtk::Label>();
        ht->set_markup("<b>" + std::string(zone_titles[z]) + "</b>");
        ht->set_halign(Gtk::Align::START);
        col->append(*ht);

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_min_content_height(220);
        scroll->set_vexpand(true);
        auto inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        for (const auto& w : ux::panel_widget_catalog())
        {
            auto chk = Gtk::make_managed<Gtk::CheckButton>(w.label);
            chk->set_tooltip_text(w.blurb);
            std::string key = std::string(zones[z]) + ":" + w.id;
            widget_checks[key] = chk;
            inner->append(*chk);
        }
        scroll->set_child(*inner);
        col->append(*scroll);
        frame->set_child(*col);
        widget_cols->append(*frame);
    }
}

void PanelPage::set_widgets_from_string(const std::string& left, const std::string& center,
    const std::string& right)
{
    for (auto& kv : widget_checks)
    {
        kv.second->set_active(false);
    }
    auto apply_zone = [&] (const std::string& zone, const std::string& s) {
        for (const auto& id : split_ws(s))
        {
            std::string key = zone + ":" + id;
            auto it = widget_checks.find(key);
            if (it != widget_checks.end())
            {
                it->second->set_active(true);
            }
        }
    };
    apply_zone("left", left);
    apply_zone("center", center);
    apply_zone("right", right);
}

std::string PanelPage::widgets_string_for(const std::string& zone) const
{
    /* Preserve catalog order for stable layout */
    std::vector<std::string> ids;
    for (const auto& w : ux::panel_widget_catalog())
    {
        std::string key = zone + ":" + w.id;
        auto it = widget_checks.find(key);
        if (it != widget_checks.end() && it->second->get_active())
        {
            ids.push_back(w.id);
        }
    }
    return join_ws(ids);
}

void PanelPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

std::string PanelPage::shell_ini_path() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/wf-shell.ini" : std::string{};
}

std::string PanelPage::resource_themes_dir() const
{
#ifdef RESOURCEDIR
    return std::string(RESOURCEDIR) + "/themes";
#else
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share/wf-shell/themes" :
                  "/usr/local/share/wf-shell/themes";
#endif
}

std::string PanelPage::user_themes_dir() const
{
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/wf-shell/themes" : std::string{};
}

void PanelPage::refresh()
{
    filling = true;
    const auto ini = shell_ini_path();
    auto packs = wf_shell::discover_theme_packs(resource_themes_dir(), user_themes_dir());
    themes = wf_shell::theme_packs_ui_order(packs);

    std::vector<Glib::ustring> labels;
    for (const auto& t : themes)
    {
        labels.push_back(t.name);
    }
    theme_drop->set_model(Gtk::StringList::create(labels));

    std::string css_path = wf_shell::get_ini_css_path(ini);
    {
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
                    css_path = cit->second;
                }
            }
        }
    }
    std::string cur_id = wf_shell::theme_id_from_css_path(css_path);
    guint prefer = 0;
    for (size_t i = 0; i < themes.size(); ++i)
    {
        if (themes[i].id == cur_id)
        {
            prefer = static_cast<guint>(i);
            break;
        }
    }
    if (!labels.empty())
    {
        theme_drop->set_selected(prefer);
    }

    std::string pos = wf_shell::ini_get(ini, "panel", "position");
    if (pos.empty())
    {
        pos = "top";
    }
    guint pidx = 0;
    for (guint i = 0; i < position_values.size(); ++i)
    {
        if (pos == position_values[i])
        {
            pidx = i;
            break;
        }
    }
    position_drop->set_selected(pidx);

    height_spin->set_value(wf_shell::ini_get_int(ini, "panel", "minimal_height", 28));
    autohide_chk->set_active(wf_shell::ini_get_bool(ini, "panel", "autohide", false));
    menu_list_chk->set_active(wf_shell::ini_get_bool(ini, "panel", "menu_list", false));
    menu_cats_chk->set_active(wf_shell::ini_get_bool(ini, "panel", "menu_show_categories", false));

    set_widgets_from_string(
        wf_shell::ini_get(ini, "panel", "widgets_left"),
        wf_shell::ini_get(ini, "panel", "widgets_center"),
        wf_shell::ini_get(ini, "panel", "widgets_right"));

    if (prefer < themes.size())
    {
        info_lbl->set_text(themes[prefer].path.empty() ?
            "Default look" : ("Theme: " + themes[prefer].name));
    }
    filling = false;
    if (status)
    {
        status->set_text("Panel ready.");
    }
}

void PanelPage::on_apply()
{
    const auto ini = shell_ini_path();
    auto tidx = theme_drop->get_selected();
    if (tidx < themes.size())
    {
        std::string err;
        if (!wf_shell::apply_theme_pack(themes[tidx].id, ini,
                resource_themes_dir(), user_themes_dir(), &err))
        {
            if (status)
            {
                status->set_text("Theme failed: " + err);
            }
            return;
        }
    }

    auto pidx = position_drop->get_selected();
    std::string pos = "top";
    if (pidx < position_values.size())
    {
        pos = position_values[pidx];
    }

    std::map<std::string, std::string> kv;
    kv["position"] = pos;
    kv["minimal_height"] = std::to_string(static_cast<int>(height_spin->get_value()));
    kv["autohide"] = autohide_chk->get_active() ? "true" : "false";
    kv["menu_list"] = menu_list_chk->get_active() ? "true" : "false";
    kv["menu_show_categories"] = menu_cats_chk->get_active() ? "true" : "false";
    kv["widgets_left"] = widgets_string_for("left");
    kv["widgets_center"] = widgets_string_for("center");
    kv["widgets_right"] = widgets_string_for("right");

    if (tidx < themes.size())
    {
        auto packs = wf_shell::discover_theme_packs(resource_themes_dir(), user_themes_dir());
        auto it = packs.find(themes[tidx].id);
        if (it != packs.end())
        {
            kv["css_path"] = it->second.path;
        } else
        {
            kv["css_path"] = "";
        }
    }
    std::string err;
    if (!wf_shell::settings_save_section("panel", kv, &err))
    {
        if (status)
        {
            status->set_text("Could not save: " + err);
        }
        return;
    }
    if (theme_applied_cb)
    {
        theme_applied_cb();
    }
    if (status)
    {
        status->set_text("Panel saved. Look and widgets update with the bar.");
    }
    refresh();
}

} // namespace wf_settings
