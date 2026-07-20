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
            status->set_text("Display discovery failed (is WAYLAND_DISPLAY set?)");
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
        status->set_text("Discovered " + std::to_string(probe.outputs.size()) +
            " output(s). Resolution and refresh stay paired to hardware modes.");
    }
}

void DisplayPage::on_output_changed()
{
    fill_resolutions();
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
            "Find monitors first, then pick a size and smoothness.";
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
        const std::string msg = "That mode is not on the hardware list.";
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
            status->set_text("Could not change display: " + err);
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
                status->set_text("Screen changed, but save failed: " + perr);
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
        status->set_text("Display set to " + mode.label() + ".");
    }
    return true;
}

} // namespace wf_settings
