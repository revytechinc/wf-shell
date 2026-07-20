#include "display-page.hpp"

#include <glibmm.h>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace wf_settings
{

DisplayPage::DisplayPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 12)
{
    set_margin(16);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Displays</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>();
    help->set_text(
        "Find your monitors, pick a size and smoothness, then Use this display mode. "
        "We only use modes your hardware lists — no guessing.");
    help->set_wrap(true);
    help->set_halign(Gtk::Align::START);
    help->add_css_class("dim-label");
    append(*help);

    /* Monitor Layout visualizer */
    layout_container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    layout_container->add_css_class("display-layout-canvas");
    layout_container->set_size_request(-1, 220);
    layout_fixed = Gtk::make_managed<Gtk::Fixed>();
    layout_fixed->set_expand(true);
    layout_container->append(*layout_fixed);
    append(*layout_container);
    layout_container->set_visible(false);

    auto out_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto out_l = Gtk::make_managed<Gtk::Label>("Monitor");
    out_l->set_halign(Gtk::Align::START);
    out_l->set_size_request(120, -1);
    output_drop = Gtk::make_managed<Gtk::DropDown>();
    output_drop->set_hexpand(true);
    out_row->append(*out_l);
    out_row->append(*output_drop);
    append(*out_row);

    auto res_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto res_l = Gtk::make_managed<Gtk::Label>("Size");
    res_l->set_halign(Gtk::Align::START);
    res_l->set_size_request(120, -1);
    res_drop = Gtk::make_managed<Gtk::DropDown>();
    res_drop->set_hexpand(true);
    res_row->append(*res_l);
    res_row->append(*res_drop);
    append(*res_row);

    auto rate_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto rate_l = Gtk::make_managed<Gtk::Label>("Smoothness");
    rate_l->set_halign(Gtk::Align::START);
    rate_l->set_size_request(120, -1);
    rate_drop = Gtk::make_managed<Gtk::DropDown>();
    rate_drop->set_hexpand(true);
    rate_row->append(*rate_l);
    rate_row->append(*rate_drop);
    append(*rate_row);

    info_lbl = Gtk::make_managed<Gtk::Label>();
    info_lbl->set_halign(Gtk::Align::START);
    info_lbl->set_wrap(true);
    info_lbl->add_css_class("dim-label");
    append(*info_lbl);

    auto actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    refresh_btn = Gtk::make_managed<Gtk::Button>("Find monitors");
    refresh_btn->set_tooltip_text("Look up what your screen can do (safe — does not change anything yet)");
    use_mode_btn = Gtk::make_managed<Gtk::Button>("Use this display mode");
    use_mode_btn->add_css_class("suggested-action");
    use_mode_btn->set_tooltip_text("Change the screen to the size and smoothness you picked");
    use_mode_btn->set_sensitive(false);
    actions->append(*refresh_btn);
    actions->append(*use_mode_btn);
    append(*actions);

    refresh_btn->signal_clicked().connect([this] () { refresh(); });
    use_mode_btn->signal_clicked().connect([this] () { save(nullptr); });

    out_conn = output_drop->property_selected().signal_changed().connect(
        [this] () {
            if (!filling_ui)
            {
                on_output_changed();
            }
        });
    res_conn = res_drop->property_selected().signal_changed().connect(
        [this] () {
            if (!filling_ui)
            {
                on_resolution_changed();
            }
        });
    rate_conn = rate_drop->property_selected().signal_changed().connect(
        [this] () {
            if (!filling_ui)
            {
                update_apply_sensitive();
                update_info();
            }
        });

    /*
     * Do NOT probe the live compositor in the constructor.
     * Opening Settings used to call wlr-randr immediately (Displays is the
     * first page), which has taken down Wayfire on this NVIDIA/FreeBSD setup.
     * Discovery is explicit: user clicks "Find monitors" or refresh() from
     * select_page after the window is up.
     */
    info_lbl->set_text(
        "Click “Find monitors” to read what the compositor reports. "
        "We never invent modes, and we do not touch the GPU until you ask.");
    /* no per-page Apply — Save is global */
}

void DisplayPage::set_status_target(Gtk::Label *status_label)
{
    status = status_label;
}

std::string DisplayPage::wayfire_ini_path() const
{
    const char *override = std::getenv("WAYFIRE_CONFIG_FILE");
    if (override && override[0])
    {
        return override;
    }
    const char *home = std::getenv("HOME");
    if (!home)
    {
        return {};
    }
    return std::string(home) + "/.config/wayfire.ini";
}

std::string DisplayPage::kanshi_path() const
{
    const char *home = std::getenv("HOME");
    if (!home)
    {
        return {};
    }
    return std::string(home) + "/.config/kanshi/config";
}

const wf_shell::DisplayOutput *DisplayPage::selected_output() const
{
    const auto idx = output_drop->get_selected();
    if (idx >= probe.outputs.size())
    {
        return nullptr;
    }
    return &probe.outputs[idx];
}

wf_shell::DisplayMode DisplayPage::selected_safe_mode() const
{
    const auto *o = selected_output();
    if (!o)
    {
        return {};
    }
    const auto ridx = res_drop->get_selected();
    const auto fidx = rate_drop->get_selected();
    if (ridx >= res_cache.size() || fidx >= rate_cache.size())
    {
        return {};
    }
    const auto& res = res_cache[ridx];
    return o->resolve_safe(res.first, res.second, rate_cache[fidx]);
}

void DisplayPage::update_apply_sensitive()
{
    if (use_mode_btn)
    {
        use_mode_btn->set_sensitive(selected_safe_mode().valid());
    }
    update_info();
}

void DisplayPage::refresh()
{
    filling_ui = true;
    probe = wf_shell::probe_displays(nullptr);
    res_cache.clear();
    rate_cache.clear();

    if (!probe.ok)
    {
        std::string msg = "Could not discover displays.";
        for (const auto& w : probe.warnings)
        {
            msg += "\n" + w;
        }
        info_lbl->set_text(msg);
        output_drop->set_model(Gtk::StringList::create({}));
        res_drop->set_model(Gtk::StringList::create({}));
        rate_drop->set_model(Gtk::StringList::create({}));
        /* no per-page Apply — Save is global */
        filling_ui = false;
        if (status)
        {
            status->set_text("We couldn't find your monitors. Please ensure Wayfire is active.");
        }
        return;
    }

    std::vector<Glib::ustring> names;
    guint prefer_out = 0;
    for (size_t i = 0; i < probe.outputs.size(); ++i)
    {
        const auto& o = probe.outputs[i];
        /* Prefer human model name; keep connector as secondary clue */
        std::string label;
        if (!o.model.empty())
        {
            label = o.model;
            if (!o.name.empty())
            {
                label += " (" + o.name + ")";
            }
        } else
        {
            label = o.name.empty() ? "Monitor" : o.name;
        }
        if (!o.enabled)
        {
            label += " — off";
        }
        names.push_back(label);
        if (o.current_mode().valid())
        {
            prefer_out = static_cast<guint>(i);
        }
    }
    output_drop->set_model(Gtk::StringList::create(names));
    if (!names.empty())
    {
        output_drop->set_selected(prefer_out);
    }
    filling_ui = false;
    on_output_changed();
    if (status)
    {
        status->set_text("✨ Found " + std::to_string(probe.outputs.size()) +
            " active monitor(s)!");
    }
}

void DisplayPage::on_output_changed()
{
    fill_resolutions();
    update_layout_visualization();
}

void DisplayPage::fill_resolutions()
{
    filling_ui = true;
    res_cache.clear();
    rate_cache.clear();
    const auto *o = selected_output();
    if (!o)
    {
        res_drop->set_model(Gtk::StringList::create({}));
        rate_drop->set_model(Gtk::StringList::create({}));
        info_lbl->set_text("");
        /* no per-page Apply — Save is global */
        filling_ui = false;
        return;
    }

    res_cache = o->unique_resolutions();
    std::vector<Glib::ustring> labels;
    guint prefer = 0;
    auto cur = o->current_mode();
    for (size_t i = 0; i < res_cache.size(); ++i)
    {
        labels.push_back(wf_shell::resolution_label(res_cache[i].first, res_cache[i].second));
        if (cur.valid() && cur.width == res_cache[i].first && cur.height == res_cache[i].second)
        {
            prefer = static_cast<guint>(i);
        }
    }
    res_drop->set_model(Gtk::StringList::create(labels));
    if (!labels.empty())
    {
        res_drop->set_selected(prefer);
    }
    filling_ui = false;
    fill_refresh_rates();
}

void DisplayPage::on_resolution_changed()
{
    fill_refresh_rates();
}

void DisplayPage::fill_refresh_rates()
{
    filling_ui = true;
    rate_cache.clear();
    const auto *o = selected_output();
    const auto ridx = res_drop->get_selected();
    if (!o || ridx >= res_cache.size())
    {
        rate_drop->set_model(Gtk::StringList::create({}));
        /* no per-page Apply — Save is global */
        filling_ui = false;
        update_info();
        return;
    }

    const int w = res_cache[ridx].first;
    const int h = res_cache[ridx].second;
    rate_cache = o->refresh_rates_for(w, h);

    std::vector<Glib::ustring> labels;
    guint prefer = 0;
    auto cur = o->current_mode();
    for (size_t i = 0; i < rate_cache.size(); ++i)
    {
        labels.push_back(wf_shell::refresh_label(rate_cache[i]));
        if (cur.valid() && cur.width == w && cur.height == h &&
            std::abs(cur.refresh_hz - rate_cache[i]) < 0.05)
        {
            prefer = static_cast<guint>(i);
        }
    }
    rate_drop->set_model(Gtk::StringList::create(labels));
    if (!labels.empty())
    {
        rate_drop->set_selected(prefer);
    }
    filling_ui = false;
    update_apply_sensitive();
    update_info();
}

void DisplayPage::update_info()
{
    const auto *o = selected_output();
    if (!o)
    {
        info_lbl->set_text("");
        return;
    }
    std::ostringstream info;
    if (!o->model.empty())
    {
        info << o->model;
        if (!o->name.empty())
        {
            info << " · " << o->name;
        }
    } else
    {
        info << o->name;
    }
    if (!o->description.empty())
    {
        info << "\n" << o->description;
    }
    if (o->physical_w_mm > 0)
    {
        const double inch = std::sqrt(
            (o->physical_w_mm / 25.4) * (o->physical_w_mm / 25.4) +
            (o->physical_h_mm / 25.4) * (o->physical_h_mm / 25.4));
        info << "\nAbout " << std::lround(inch) << "″ panel";
    }
    auto m = selected_safe_mode();
    if (m.valid())
    {
        info << "\nWill apply: " << m.label();
    } else if (!res_cache.empty())
    {
        info << "\nPick a size and smoothness that belong together.";
    }
    info_lbl->set_text(info.str());
}

bool DisplayPage::save(std::string *error)
{
    const auto *o = selected_output();
    auto mode = selected_safe_mode();
    if (!o || !mode.valid())
    {
        const std::string msg =
            "Please choose a monitor to configure first.";
        if (error)
        {
            *error = msg;
        }
        if (status)
        {
            status->set_text(msg);
        }
        return false;
    }
    /* TAOCP: never trust UI alone — re-check against discovered modes. */
    if (!o->supports(mode))
    {
        const std::string msg = "This screen size or refresh rate is not supported by your monitor.";
        if (error)
        {
            *error = msg;
        }
        if (status)
        {
            status->set_text(msg);
        }
        return false;
    }

    auto output = *o;
    std::string err;

    if (!wf_shell::apply_display_mode(output, mode, nullptr, &err))
    {
        if (error)
        {
            *error = err;
        }
        if (status)
        {
            status->set_text("We couldn't update your screen settings: " + err);
        }
        return false;
    }

    std::string perr;
    if (!wayfire_ini_path().empty())
    {
        if (!wf_shell::persist_output_to_wayfire_ini(wayfire_ini_path(), output, mode,
                nullptr, &perr))
        {
            if (error)
            {
                *error = perr;
            }
            if (status)
            {
                status->set_text("Screen configuration applied, but we couldn't write it to disk: " + perr);
            }
            return false;
        }
    }
    if (!kanshi_path().empty())
    {
        std::string kerr;
        (void)wf_shell::persist_output_to_kanshi(kanshi_path(), output, mode,
            nullptr, &kerr);
    }

    if (status)
    {
        status->set_text("✨ Display resolution successfully set to " + mode.label() + "!");
    }
    return true;
}

void DisplayPage::update_layout_visualization()
{
    // Clear previous buttons
    for (auto btn : monitor_buttons)
    {
        layout_fixed->remove(*btn);
    }
    monitor_buttons.clear();

    if (!probe.ok || probe.outputs.empty())
    {
        layout_container->set_visible(false);
        return;
    }

    layout_container->set_visible(true);

    double min_x = 999999, max_x = -999999;
    double min_y = 999999, max_y = -999999;
    for (const auto& o : probe.outputs)
    {
        int w = 1920, h = 1080;
        auto cur = o.current_mode();
        if (cur.valid())
        {
            w = cur.width;
            h = cur.height;
        }
        double x = o.pos_x;
        double y = o.pos_y;
        double width = w / o.scale;
        double height = h / o.scale;

        if (x < min_x) min_x = x;
        if (y < min_y) min_y = y;
        if (x + width > max_x) max_x = x + width;
        if (y + height > max_y) max_y = y + height;
    }

    if (max_x <= min_x || max_y <= min_y)
    {
        return;
    }

    double canvas_w = 460;
    double canvas_h = 200;
    double total_w = max_x - min_x;
    double total_h = max_y - min_y;

    double scale_x = (canvas_w - 40) / total_w;
    double scale_y = (canvas_h - 40) / total_h;
    double scale = std::min(scale_x, scale_y);

    if (scale > 0.08) scale = 0.08;
    if (scale < 0.005) scale = 0.02;

    double offset_x = 20 + (canvas_w - 40 - total_w * scale) / 2.0;
    double offset_y = 20 + (canvas_h - 40 - total_h * scale) / 2.0;

    auto current_sel = output_drop->get_selected();

    for (size_t i = 0; i < probe.outputs.size(); ++i)
    {
        const auto& o = probe.outputs[i];
        int w = 1920, h = 1080;
        auto cur = o.current_mode();
        if (cur.valid())
        {
            w = cur.width;
            h = cur.height;
        }

        double box_w = (w / o.scale) * scale;
        double box_h = (h / o.scale) * scale;
        double box_x = offset_x + (o.pos_x - min_x) * scale;
        double box_y = offset_y + (o.pos_y - min_y) * scale;

        if (box_w < 100) box_w = 100;
        if (box_h < 60) box_h = 60;

        auto btn = Gtk::make_managed<Gtk::Button>();
        btn->add_css_class("monitor-box");
        if (i == current_sel)
        {
            btn->add_css_class("selected-monitor");
        }

        auto box_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box_content->set_valign(Gtk::Align::CENTER);
        box_content->set_halign(Gtk::Align::CENTER);

        auto lbl_title = Gtk::make_managed<Gtk::Label>();
        lbl_title->set_markup("<b>" + o.name + "</b>");
        lbl_title->add_css_class("monitor-title");
        box_content->append(*lbl_title);

        auto lbl_sub = Gtk::make_managed<Gtk::Label>(std::to_string(w) + "×" + std::to_string(h));
        lbl_sub->add_css_class("monitor-sub");
        box_content->append(*lbl_sub);

        btn->set_child(*box_content);
        btn->set_size_request(static_cast<int>(box_w), static_cast<int>(box_h));

        btn->signal_clicked().connect([this, i] () {
            output_drop->set_selected(static_cast<guint>(i));
        });

        layout_fixed->put(*btn, box_x, box_y);
        monitor_buttons.push_back(btn);
    }
}

} // namespace wf_settings
