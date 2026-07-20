#include "panel-page.hpp"

#include "apply-gate.hpp"
#include "panel-capabilities.hpp"
#include "ini-file.hpp"
#include "shell-json-config.hpp"
#include "theme-defaults.hpp"
#include "ux-labels.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

#include <glibmm/main.h>

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

std::string friendly_widget(const std::string& id)
{
    auto l = ux::widget_label(id);
    return l.empty() ? id : l;
}

} // namespace

PanelPage::PanelPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 16)
{
    set_margin(20);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<span size='large'><b>Panel</b></span>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "Choose how the bar looks and what sits on it. "
        "Pick items from the lists — you never type names.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    help->set_margin_bottom(4);
    append(*help);

    /* ── Look ─────────────────────────────────────────────────────────── */
    auto look_frame = Gtk::make_managed<Gtk::Frame>("Look");
    auto look = Gtk::make_managed<Gtk::Grid>();
    look->set_margin(14);
    look->set_column_spacing(16);
    look->set_row_spacing(12);
    int row = 0;

    auto add_label = [&] (const char *text) {
        auto l = Gtk::make_managed<Gtk::Label>(text);
        l->set_halign(Gtk::Align::START);
        l->set_size_request(120, -1);
        look->attach(*l, 0, row, 1, 1);
    };

    add_label("Theme");
    theme_drop = Gtk::make_managed<Gtk::DropDown>();
    theme_drop->set_hexpand(true);
    theme_drop->set_tooltip_text("Color scheme for the panel and menus");
    look->attach(*theme_drop, 1, row++, 1, 1);

    add_label("Bar position");
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
    look->attach(*position_drop, 1, row++, 1, 1);

    add_label("Thickness");
    height_spin = Gtk::make_managed<Gtk::SpinButton>();
    height_spin->set_range(16, 128);
    height_spin->set_increments(1, 4);
    height_spin->set_digits(0);
    height_spin->set_tooltip_text("How thick the bar is, in pixels");
    look->attach(*height_spin, 1, row++, 1, 1);

    autohide_chk = Gtk::make_managed<Gtk::CheckButton>(
        "Hide the bar until I move the pointer to the edge");
    look->attach(*autohide_chk, 0, row++, 2, 1);

    menu_list_chk = Gtk::make_managed<Gtk::CheckButton>("Show the app menu as a list");
    look->attach(*menu_list_chk, 0, row++, 2, 1);

    menu_cats_chk = Gtk::make_managed<Gtk::CheckButton>("Group apps by category in the menu");
    look->attach(*menu_cats_chk, 0, row++, 2, 1);

    look_frame->set_child(*look);
    append(*look_frame);

    /* ── Live preview of layout (plain text, always safe) ─────────────── */
    auto prev_frame = Gtk::make_managed<Gtk::Frame>("Your bar (preview)");
    auto prev_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    prev_box->set_margin(14);
    bar_preview = Gtk::make_managed<Gtk::Label>();
    bar_preview->set_wrap(true);
    bar_preview->set_halign(Gtk::Align::START);
    bar_preview->set_xalign(0);
    bar_preview->add_css_class("dim-label");
    prev_box->append(*bar_preview);
    prev_frame->set_child(*prev_box);
    append(*prev_frame);

    /* ── What’s on the bar ────────────────────────────────────────────── */
    auto wtitle = Gtk::make_managed<Gtk::Label>();
    wtitle->set_markup("<b>What’s on the bar</b>");
    wtitle->set_halign(Gtk::Align::START);
    append(*wtitle);

    auto whint = Gtk::make_managed<Gtk::Label>(
        "Add Clock, Volume, Network, and friends to the left, middle, or right. "
        "Use ↑ ↓ to reorder. Gaps (Small gap / Medium gap) add breathing room.");
    whint->set_wrap(true);
    whint->add_css_class("dim-label");
    whint->set_halign(Gtk::Align::START);
    append(*whint);

    widget_cols = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    widget_cols->set_homogeneous(true);
    widget_cols->set_vexpand(true);
    append(*widget_cols);
    rebuild_zone_ui();

    info_lbl = Gtk::make_managed<Gtk::Label>();
    info_lbl->set_halign(Gtk::Align::START);
    info_lbl->set_wrap(true);
    info_lbl->add_css_class("dim-label");
    info_lbl->set_margin_top(4);
    append(*info_lbl);

    /*
     * Modeless prefs, but NEVER apply without validation — and debounce so
     * spinning the theme list cannot thrash/crash the panel.
     */
    auto live = [this] () {
        if (filling)
        {
            return;
        }
        schedule_live_save();
    };
    theme_drop->property_selected().signal_changed().connect([this, live] () {
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
        live();
    });
    position_drop->property_selected().signal_changed().connect(live);
    height_spin->signal_value_changed().connect(live);
    autohide_chk->signal_toggled().connect(live);
    menu_list_chk->signal_toggled().connect(live);
    menu_cats_chk->signal_toggled().connect(live);

    refresh();
}

void PanelPage::schedule_live_save()
{
    if (live_save_debounce.connected())
    {
        live_save_debounce.disconnect();
    }
    wf_shell::gate_log("panel-page", "debounce live save 350ms");
    live_save_debounce = Glib::signal_timeout().connect(
        [this] () {
            std::string err;
            if (!save(&err) && status)
            {
                status->set_text(err.empty() ? "Could not apply panel settings." : err);
            }
            return false;
        },
        350);
}

void PanelPage::rebuild_zone_ui()
{
    auto kids = widget_cols->get_children();
    for (auto *c : kids)
    {
        widget_cols->remove(*c);
    }
    zone_lists.clear();
    zone_add_drops.clear();
    zone_add_ids.clear();

    const char *zones[] = {"left", "center", "right"};
    const char *zone_titles[] = {"Left side", "Middle", "Right side"};

    for (int z = 0; z < 3; ++z)
    {
        const std::string zone = zones[z];
        auto frame = Gtk::make_managed<Gtk::Frame>(zone_titles[z]);
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        col->set_margin(12);

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_min_content_height(200);
        scroll->set_vexpand(true);
        auto list = Gtk::make_managed<Gtk::ListBox>();
        list->set_selection_mode(Gtk::SelectionMode::SINGLE);
        list->add_css_class("rich-list");
        scroll->set_child(*list);
        col->append(*scroll);
        zone_lists[zone] = list;

        auto btns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto up = Gtk::make_managed<Gtk::Button>("↑");
        auto down = Gtk::make_managed<Gtk::Button>("↓");
        auto rem = Gtk::make_managed<Gtk::Button>("Remove");
        up->set_tooltip_text("Move selected item earlier on this side");
        down->set_tooltip_text("Move selected item later on this side");
        rem->set_tooltip_text("Take selected item off the bar");
        up->set_hexpand(true);
        down->set_hexpand(true);
        rem->set_hexpand(true);
        btns->append(*up);
        btns->append(*down);
        btns->append(*rem);
        col->append(*btns);

        up->signal_clicked().connect([this, zone] () { move_selected(zone, -1); });
        down->signal_clicked().connect([this, zone] () { move_selected(zone, +1); });
        rem->signal_clicked().connect([this, zone] () { remove_selected(zone); });

        auto add_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto add_drop = Gtk::make_managed<Gtk::DropDown>();
        add_drop->set_hexpand(true);
        add_drop->set_tooltip_text("Choose something to put on this side");
        auto add_btn = Gtk::make_managed<Gtk::Button>("Add");
        add_btn->add_css_class("suggested-action");
        add_row->append(*add_drop);
        add_row->append(*add_btn);
        col->append(*add_row);
        zone_add_drops[zone] = add_drop;

        /* Fill add dropdown with full catalog */
        {
            std::vector<Glib::ustring> labels;
            std::vector<std::string> ids;
            labels.emplace_back("Choose an item…");
            ids.emplace_back("");
            for (const auto& w : ux::panel_widget_catalog())
            {
                labels.push_back(w.label);
                ids.push_back(w.id);
            }
            add_drop->set_model(Gtk::StringList::create(labels));
            add_drop->set_selected(0);
            zone_add_ids[zone] = ids;
        }

        add_btn->signal_clicked().connect([this, zone] () {
            auto *drop = zone_add_drops[zone];
            auto& ids = zone_add_ids[zone];
            auto idx = drop->get_selected();
            if (idx == GTK_INVALID_LIST_POSITION || idx >= ids.size() || ids[idx].empty())
            {
                if (status)
                {
                    status->set_text("Please select a widget from the list to add it to your panel.");
                }
                return;
            }
            add_widget_to_zone(zone, ids[idx]);
            drop->set_selected(0);
        });

        frame->set_child(*col);
        widget_cols->append(*frame);
    }
}

void PanelPage::refill_zone_list(const std::string& zone)
{
    auto it = zone_lists.find(zone);
    if (it == zone_lists.end() || !it->second)
    {
        return;
    }
    auto *list = it->second;
    while (auto *row = list->get_row_at_index(0))
    {
        list->remove(*row);
    }
    for (const auto& id : zone_widgets[zone])
    {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin_start(8);
        box->set_margin_end(8);
        box->set_margin_top(6);
        box->set_margin_bottom(6);
        auto name = Gtk::make_managed<Gtk::Label>(friendly_widget(id));
        name->set_halign(Gtk::Align::START);
        name->set_xalign(0);
        box->append(*name);
        auto blurb = ux::widget_blurb(id);
        if (!blurb.empty())
        {
            auto b = Gtk::make_managed<Gtk::Label>(blurb);
            b->set_halign(Gtk::Align::START);
            b->set_xalign(0);
            b->add_css_class("dim-label");
            b->set_wrap(true);
            box->append(*b);
        }
        row->set_child(*box);
        row->set_name(id);
        list->append(*row);
    }
}

void PanelPage::update_bar_preview()
{
    if (!bar_preview)
    {
        return;
    }
    auto fmt = [&] (const std::string& zone) {
        const auto& v = zone_widgets[zone];
        if (v.empty())
        {
            return std::string("(empty)");
        }
        std::ostringstream o;
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i)
            {
                o << " · ";
            }
            o << friendly_widget(v[i]);
        }
        return o.str();
    };
    bar_preview->set_text(
        "Left: " + fmt("left") + "\n"
        "Middle: " + fmt("center") + "\n"
        "Right: " + fmt("right"));
}

void PanelPage::add_widget_to_zone(const std::string& zone, const std::string& id)
{
    if (id.empty())
    {
        return;
    }
    zone_widgets[zone].push_back(id);
    refill_zone_list(zone);
    update_bar_preview();
    if (status)
    {
        schedule_live_save();
    }
}

void PanelPage::remove_selected(const std::string& zone)
{
    auto *list = zone_lists[zone];
    if (!list)
    {
        return;
    }
    auto *row = list->get_selected_row();
    if (!row)
    {
        if (status)
        {
            status->set_text("Please select a widget from your layout list first to configure it.");
        }
        return;
    }
    auto id = row->get_name();
    auto& v = zone_widgets[zone];
    auto it = std::find(v.begin(), v.end(), id);
    /* Prefer index of selected row (handles duplicate spacers) */
    int idx = row->get_index();
    if (idx >= 0 && static_cast<size_t>(idx) < v.size())
    {
        v.erase(v.begin() + idx);
    } else if (it != v.end())
    {
        v.erase(it);
    }
    refill_zone_list(zone);
    update_bar_preview();
    if (!filling)
    {
        schedule_live_save();
    }
}

void PanelPage::move_selected(const std::string& zone, int delta)
{
    auto *list = zone_lists[zone];
    if (!list)
    {
        return;
    }
    auto *row = list->get_selected_row();
    if (!row)
    {
        return;
    }
    int idx = row->get_index();
    auto& v = zone_widgets[zone];
    int n = static_cast<int>(v.size());
    int j = idx + delta;
    if (idx < 0 || j < 0 || j >= n)
    {
        return;
    }
    std::swap(v[idx], v[j]);
    refill_zone_list(zone);
    if (auto *nr = zone_lists[zone]->get_row_at_index(j))
    {
        zone_lists[zone]->select_row(*nr);
    }
    update_bar_preview();
    if (!filling)
    {
        schedule_live_save();
    }
}

void PanelPage::set_widgets_from_string(const std::string& left, const std::string& center,
    const std::string& right)
{
    auto clean = [] (const std::string& s) {
        return split_ws(wf_shell::sanitize_panel_widgets_list(s));
    };
    zone_widgets["left"] = clean(left);
    zone_widgets["center"] = clean(center);
    zone_widgets["right"] = clean(right);
    for (const char *z : {"left", "center", "right"})
    {
        refill_zone_list(z);
    }
    update_bar_preview();
}

std::string PanelPage::widgets_string_for(const std::string& zone) const
{
    auto it = zone_widgets.find(zone);
    if (it == zone_widgets.end())
    {
        return "none";
    }
    return join_ws(it->second);
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
    {
        std::string d = std::string(RESOURCEDIR) + "/themes";
        if (std::filesystem::is_directory(d))
        {
            return d;
        }
    }
#endif
    const char *home = std::getenv("HOME");
    if (home)
    {
        std::string d = std::string(home) + "/.local/share/wf-shell/themes";
        if (std::filesystem::is_directory(d))
        {
            return d;
        }
    }
    if (std::filesystem::is_directory("/usr/local/share/wf-shell/themes"))
    {
        return "/usr/local/share/wf-shell/themes";
    }
    return "/usr/local/share/wf-shell/themes";
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
        status->set_text(""); /* modeless: quiet when idle */
    }
}

bool PanelPage::save(std::string *error)
{
    if (saving)
    {
        wf_shell::gate_log("panel-page", "save reentrant — skip");
        return true;
    }
    saving = true;

    const auto ini = shell_ini_path();
    auto tidx = theme_drop->get_selected();
    std::string theme_id = "default";
    std::string css_path;

    if (tidx < themes.size())
    {
        theme_id = themes[tidx].id;
        css_path = themes[tidx].path;
    }

    wf_shell::gate_log("panel-page", "save begin theme=" + theme_id +
        " css=" + css_path + " ini=" + ini);

    /* Gate theme before any disk write. */
    auto tgate = wf_shell::validate_theme_apply(theme_id, css_path);
    if (!tgate.ok)
    {
        if (error)
        {
            *error = tgate.summary();
        }
        if (status)
        {
            status->set_text("We couldn't apply this theme: " + tgate.summary());
        }
        wf_shell::gate_log_result("panel-page", tgate);
        saving = false;
        return false;
    }

    if (tidx < themes.size())
    {
        std::string err;
        if (!wf_shell::apply_theme_pack(themes[tidx].id, ini,
                resource_themes_dir(), user_themes_dir(), &err))
        {
            if (error)
            {
                *error = err;
            }
            if (status)
            {
                status->set_text("Oops, we ran into an issue applying this theme: " + err);
            }
            saving = false;
            return false;
        }
    }

    auto pidx = position_drop->get_selected();
    std::string pos = "top";
    if (pidx < position_values.size())
    {
        pos = position_values[pidx];
    }
    /* Validate position is one of known values */
    bool pos_ok = false;
    for (const auto& p : position_values)
    {
        if (p == pos)
        {
            pos_ok = true;
            break;
        }
    }
    if (!pos_ok)
    {
        if (error)
        {
            *error = "invalid panel position";
        }
        saving = false;
        return false;
    }

    int height = static_cast<int>(height_spin->get_value());
    if (height < 16 || height > 128)
    {
        if (error)
        {
            *error = "panel thickness out of range (16–128)";
        }
        saving = false;
        return false;
    }

    std::map<std::string, std::string> kv;
    kv["position"] = pos;
    kv["minimal_height"] = std::to_string(height);
    kv["autohide"] = autohide_chk->get_active() ? "true" : "false";
    kv["menu_list"] = menu_list_chk->get_active() ? "true" : "false";
    kv["menu_show_categories"] = menu_cats_chk->get_active() ? "true" : "false";
    /* Never persist widgets this build cannot host (e.g. weather). */
    kv["widgets_left"] = wf_shell::sanitize_panel_widgets_list(widgets_string_for("left"));
    kv["widgets_center"] = wf_shell::sanitize_panel_widgets_list(widgets_string_for("center"));
    kv["widgets_right"] = wf_shell::sanitize_panel_widgets_list(widgets_string_for("right"));
    kv["css_path"] = css_path;

    wf_shell::gate_log("panel-page", "writing panel section L=\"" +
        kv["widgets_left"] + "\" C=\"" + kv["widgets_center"] +
        "\" R=\"" + kv["widgets_right"] + "\"");

    std::string err;
    if (!wf_shell::settings_save_section("panel", kv, &err))
    {
        if (error)
        {
            *error = err;
        }
        if (status)
        {
            status->set_text("Could not save: " + err);
        }
        saving = false;
        return false;
    }
    if (theme_applied_cb)
    {
        theme_applied_cb();
    }
    if (status)
    {
        status->set_text("✨ Panel settings updated successfully!");
    }
    wf_shell::gate_log("panel-page", "save ok");
    saving = false;
    return true;
}

} // namespace wf_settings
