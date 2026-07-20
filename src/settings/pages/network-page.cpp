#include "network-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace wf_settings
{
namespace
{

const char *kStatusRaw[] = {"none", "connection", "icon", "full"};
const char *kStatusLabel[] = {
    "Icon only (quiet)",
    "Show connection name",
    "Icon with signal look",
    "Full details on the bar",
};

} // namespace

NetworkPage::NetworkPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 16)
{
    set_margin(20);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<span size='large'><b>Network</b></span>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "How the network icon looks on the panel. "
        "Join Wi‑Fi and manage connections from the icon on the bar — "
        "not from this page.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(16);
    int r = 0;

    auto lab = [&] (const char *t) {
        auto l = Gtk::make_managed<Gtk::Label>(t);
        l->set_halign(Gtk::Align::START);
        l->set_size_request(140, -1);
        grid->attach(*l, 0, r, 1, 1);
    };

    lab("Icon size");
    icon_size = Gtk::make_managed<Gtk::SpinButton>();
    icon_size->set_range(12, 96);
    icon_size->set_increments(1, 4);
    icon_size->set_tooltip_text("Pixels — try 24 or 32");
    grid->attach(*icon_size, 1, r++, 1, 1);

    lab("What to show");
    net_status = Gtk::make_managed<Gtk::DropDown>();
    {
        std::vector<Glib::ustring> labels;
        for (auto *s : kStatusLabel)
        {
            labels.emplace_back(s);
        }
        net_status->set_model(Gtk::StringList::create(labels));
    }
    net_status->set_hexpand(true);
    grid->attach(*net_status, 1, r++, 1, 1);

    invert_icon = Gtk::make_managed<Gtk::CheckButton>("Flip icon colors for dark/light bars");
    grid->attach(*invert_icon, 0, r++, 2, 1);
    use_color = Gtk::make_managed<Gtk::CheckButton>("Color the icon by connection status");
    grid->attach(*use_color, 0, r++, 2, 1);
    no_label = Gtk::make_managed<Gtk::CheckButton>("Hide the text next to the icon");
    grid->attach(*no_label, 0, r++, 2, 1);

    /* Advanced: keep but hide behind a clear label — still selectable, not required */
    auto adv = Gtk::make_managed<Gtk::Expander>("Advanced (optional)");
    auto adv_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    adv_box->set_margin_top(8);
    auto al = Gtk::make_managed<Gtk::Label>(
        "Command when the icon is clicked (leave empty for the built-in menu):");
    al->set_wrap(true);
    al->set_halign(Gtk::Align::START);
    al->add_css_class("dim-label");
    adv_box->append(*al);
    onclick = Gtk::make_managed<Gtk::Entry>();
    onclick->set_hexpand(true);
    onclick->set_placeholder_text("Usually leave this empty");
    adv_box->append(*onclick);
    adv->set_child(*adv_box);
    grid->attach(*adv, 0, r++, 2, 1);

    append(*grid);

    auto live = [this] () {
        if (!filling)
        {
            save(nullptr);
        }
    };
    icon_size->signal_value_changed().connect(live);
    net_status->property_selected().signal_changed().connect(live);
    invert_icon->signal_toggled().connect(live);
    use_color->signal_toggled().connect(live);
    no_label->signal_toggled().connect(live);
    onclick->signal_changed().connect(live);

    refresh();
}

void NetworkPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

std::string NetworkPage::shell_ini() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

void NetworkPage::refresh()
{
    filling = true;
    auto ini = shell_ini();
    icon_size->set_value(wf_shell::ini_get_int(ini, "panel", "network_icon_size", 32));
    invert_icon->set_active(wf_shell::ini_get_bool(ini, "panel", "network_icon_invert_color", false));
    use_color->set_active(wf_shell::ini_get_bool(ini, "panel", "network_status_use_color", false));
    no_label->set_active(wf_shell::ini_get_bool(ini, "panel", "network_no_label", false));
    onclick->set_text(wf_shell::ini_get(ini, "panel", "network_onclick_command"));
    std::string st = wf_shell::ini_get(ini, "panel", "network_status");
    guint si = 1;
    for (guint i = 0; i < 4; ++i)
    {
        if (st == kStatusRaw[i])
        {
            si = i;
            break;
        }
    }
    net_status->set_selected(si);
    filling = false;
    if (status)
    {
        status->set_text("");
    }
}

bool NetworkPage::save(std::string *error)
{
    auto si = net_status->get_selected();
    std::map<std::string, std::string> kv;
    kv["network_icon_size"] = std::to_string(static_cast<int>(icon_size->get_value()));
    kv["network_icon_invert_color"] = invert_icon->get_active() ? "true" : "false";
    kv["network_status_use_color"] = use_color->get_active() ? "true" : "false";
    kv["network_no_label"] = no_label->get_active() ? "true" : "false";
    kv["network_onclick_command"] = onclick->get_text();
    kv["network_status"] = (si < 4) ? kStatusRaw[si] : "connection";
    std::string err;
    if (!wf_shell::settings_save_section("panel", kv, &err))
    {
        if (status)
        {
            status->set_text("Could not save: " + err);
        }
        return false;
    }
    if (status)
    {
        status->set_text("Network updated.");
    }
    return true;
}

} // namespace wf_settings
