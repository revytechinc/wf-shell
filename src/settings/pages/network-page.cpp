#include "network-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace wf_settings
{

NetworkPage::NetworkPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 10)
{
    set_margin(16);
    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Network</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "Panel network widget options (wf-shell.ini). Connection management stays in the panel popover.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(10);
    int r = 0;

    grid->attach(*Gtk::make_managed<Gtk::Label>("Icon size"), 0, r, 1, 1);
    icon_size = Gtk::make_managed<Gtk::SpinButton>();
    icon_size->set_range(12, 96);
    icon_size->set_increments(1, 4);
    grid->attach(*icon_size, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Status display"), 0, r, 1, 1);
    net_status = Gtk::make_managed<Gtk::DropDown>();
    net_status->set_model(Gtk::StringList::create({
        "none", "connection", "icon", "full"
    }));
    grid->attach(*net_status, 1, r++, 1, 1);

    invert_icon = Gtk::make_managed<Gtk::CheckButton>("Invert icon color");
    grid->attach(*invert_icon, 0, r++, 2, 1);
    use_color = Gtk::make_managed<Gtk::CheckButton>("Use status color");
    grid->attach(*use_color, 0, r++, 2, 1);
    no_label = Gtk::make_managed<Gtk::CheckButton>("Hide text label");
    grid->attach(*no_label, 0, r++, 2, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("On-click command"), 0, r, 1, 1);
    onclick = Gtk::make_managed<Gtk::Entry>();
    onclick->set_hexpand(true);
    grid->attach(*onclick, 1, r++, 1, 1);

    append(*grid);
    apply_btn = Gtk::make_managed<Gtk::Button>("Apply");
    apply_btn->add_css_class("suggested-action");
    apply_btn->set_halign(Gtk::Align::START);
    append(*apply_btn);
    apply_btn->signal_clicked().connect([this] () { on_apply(); });
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
    auto ini = shell_ini();
    icon_size->set_value(wf_shell::ini_get_int(ini, "panel", "network_icon_size", 32));
    invert_icon->set_active(wf_shell::ini_get_bool(ini, "panel", "network_icon_invert_color", false));
    use_color->set_active(wf_shell::ini_get_bool(ini, "panel", "network_status_use_color", false));
    no_label->set_active(wf_shell::ini_get_bool(ini, "panel", "network_no_label", false));
    onclick->set_text(wf_shell::ini_get(ini, "panel", "network_onclick_command"));
    std::string st = wf_shell::ini_get(ini, "panel", "network_status");
    const char *opts[] = {"none", "connection", "icon", "full"};
    guint si = 1;
    for (guint i = 0; i < 4; ++i)
    {
        if (st == opts[i])
        {
            si = i;
            break;
        }
    }
    net_status->set_selected(si);
}

void NetworkPage::on_apply()
{
    auto ini = shell_ini();
    const char *opts[] = {"none", "connection", "icon", "full"};
    auto si = net_status->get_selected();
    std::map<std::string, std::string> kv;
    kv["network_icon_size"] = std::to_string(static_cast<int>(icon_size->get_value()));
    kv["network_icon_invert_color"] = invert_icon->get_active() ? "true" : "false";
    kv["network_status_use_color"] = use_color->get_active() ? "true" : "false";
    kv["network_no_label"] = no_label->get_active() ? "true" : "false";
    kv["network_onclick_command"] = onclick->get_text();
    kv["network_status"] = (si < 4) ? opts[si] : "connection";
    std::string err;
    if (!wf_shell::settings_save_section("panel", kv, &err))
    {
        if (status)
        {
            status->set_text("Failed: " + err);
        }
        return;
    }
    if (status)
    {
        status->set_text("Network saved → config.json + legacy ini");
    }
}

} // namespace wf_settings
