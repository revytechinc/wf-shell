#include "dock-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace wf_settings
{

DockPage::DockPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 10)
{
    set_margin(16);
    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Dock</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(10);
    int r = 0;

    grid->attach(*Gtk::make_managed<Gtk::Label>("Position"), 0, r, 1, 1);
    position = Gtk::make_managed<Gtk::DropDown>();
    position->set_model(Gtk::StringList::create({"bottom", "top", "left", "right"}));
    grid->attach(*position, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Icon height"), 0, r, 1, 1);
    icon_h = Gtk::make_managed<Gtk::SpinButton>();
    icon_h->set_range(16, 128);
    icon_h->set_increments(1, 4);
    grid->attach(*icon_h, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Dock height"), 0, r, 1, 1);
    dock_h = Gtk::make_managed<Gtk::SpinButton>();
    dock_h->set_range(32, 200);
    dock_h->set_increments(1, 4);
    grid->attach(*dock_h, 1, r++, 1, 1);

    autohide = Gtk::make_managed<Gtk::CheckButton>("Autohide dock");
    grid->attach(*autohide, 0, r++, 2, 1);
    append(*grid);

    auto live = [this] () {
        if (!filling)
        {
            save(nullptr);
        }
    };
    position->property_selected().signal_changed().connect(live);
    icon_h->signal_value_changed().connect(live);
    dock_h->signal_value_changed().connect(live);
    autohide->signal_toggled().connect(live);

    refresh();
}

void DockPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

std::string DockPage::shell_ini() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

void DockPage::refresh()
{
    filling = true;
    auto ini = shell_ini();
    std::string pos = wf_shell::ini_get(ini, "dock", "position");
    const char *opts[] = {"bottom", "top", "left", "right"};
    guint pi = 0;
    for (guint i = 0; i < 4; ++i)
    {
        if (pos == opts[i])
        {
            pi = i;
            break;
        }
    }
    position->set_selected(pi);
    icon_h->set_value(wf_shell::ini_get_int(ini, "dock", "icon_height", 48));
    dock_h->set_value(wf_shell::ini_get_int(ini, "dock", "dock_height", 100));
    autohide->set_active(wf_shell::ini_get_bool(ini, "dock", "autohide", true));
    filling = false;
}

bool DockPage::save(std::string *error)
{
    auto ini = shell_ini();
    const char *opts[] = {"bottom", "top", "left", "right"};
    auto pi = position->get_selected();
    std::map<std::string, std::string> kv;
    kv["position"] = (pi < 4) ? opts[pi] : "bottom";
    kv["icon_height"] = std::to_string(static_cast<int>(icon_h->get_value()));
    kv["dock_height"] = std::to_string(static_cast<int>(dock_h->get_value()));
    kv["autohide"] = autohide->get_active() ? "true" : "false";
    std::string err;
    if (!wf_shell::settings_save_section("dock", kv, &err))
    {
        if (status)
        {
            status->set_text("Failed: " + err);
        }
        return false;
    }
    if (status)
    {
        status->set_text("Dock updated.");
    }
    return true;
}

} // namespace wf_settings
