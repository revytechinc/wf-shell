#include "session-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"
#include "power-controller.hpp"

#include <cstdlib>
#include <map>

namespace wf_settings
{
namespace
{

struct ActionSpec
{
    WFPowerController::Action action;
    const char *config_key;
    const char *title;
    const char *fallback_note;
};

const ActionSpec kActions[] = {
    {WFPowerController::Action::Shutdown, "shutdown_command", "Shut down",
        "Turn the computer completely off"},
    {WFPowerController::Action::Reboot, "reboot_command", "Restart",
        "Reboot the computer"},
    {WFPowerController::Action::Suspend, "suspend_command", "Sleep",
        "Pause and use little power (wake with a key)"},
    {WFPowerController::Action::Hibernate, "hibernate_command", "Hibernate",
        "Save state to disk and power off"},
    {WFPowerController::Action::SwitchUser, "switchuser_command", "Switch user",
        "Go to the login screen for another person"},
};

} // namespace

SessionPage::SessionPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 12)
{
    set_margin(16);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Power &amp; session</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    help = Gtk::make_managed<Gtk::Label>(
        "These are the buttons in the logout menu. "
        "We detect what this computer can do — you don’t need to type commands.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    /* Logout is special: wayland-logout is the shell session exit, not OS power */
    list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    append(*list_box);
    refresh();
    /* Checkboxes connected in rebuild_rows for live commit */
}

void SessionPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

std::string SessionPage::shell_ini() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

void SessionPage::rebuild_rows()
{
    auto kids = list_box->get_children();
    for (auto *c : kids)
    {
        list_box->remove(*c);
    }
    rows.clear();

    auto ctrl = WFPowerController::create();

    /* Logout — leave Wayfire session (not OS power) */
    {
        ActionRow r;
        r.config_key = "logout_command";
        r.title = "Log out";
        r.discovered = "wayland-logout";
        r.available = WFPowerController::check_permission("wayland-logout") ||
            WFPowerController::check_permission("/usr/local/bin/wayland-logout");
        if (!r.available)
        {
            /* still offer it — binary may exist without --help succeeding */
            r.available = true;
            r.discovered = "wayland-logout";
        }
        r.permitted = true;

        auto frame = Gtk::make_managed<Gtk::Frame>();
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        col->set_margin(10);
        auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto name = Gtk::make_managed<Gtk::Label>();
        name->set_markup("<b>Log out</b>");
        name->set_halign(Gtk::Align::START);
        name->set_hexpand(true);
        head->append(*name);
        r.status_lbl = Gtk::make_managed<Gtk::Label>("Ready");
        r.status_lbl->add_css_class("dim-label");
        head->append(*r.status_lbl);
        col->append(*head);
        r.detail_lbl = Gtk::make_managed<Gtk::Label>(
            "Leave the desktop and return to the login screen.");
        r.detail_lbl->set_wrap(true);
        r.detail_lbl->set_halign(Gtk::Align::START);
        r.detail_lbl->add_css_class("dim-label");
        col->append(*r.detail_lbl);
        r.use_chk = Gtk::make_managed<Gtk::CheckButton>("Show this button");
        r.use_chk->set_active(true);
        r.use_chk->signal_toggled().connect([this] () { save(nullptr); });
        col->append(*r.use_chk);
        frame->set_child(*col);
        list_box->append(*frame);
        rows.push_back(r);
    }

    for (const auto& spec : kActions)
    {
        ActionRow r;
        r.config_key = spec.config_key;
        r.title = spec.title;
        auto cap = ctrl->query(spec.action);
        r.available = cap.available;
        r.permitted = cap.permitted;
        r.discovered = cap.command;

        auto frame = Gtk::make_managed<Gtk::Frame>();
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        col->set_margin(10);

        auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto name = Gtk::make_managed<Gtk::Label>();
        name->set_markup("<b>" + Glib::Markup::escape_text(spec.title) + "</b>");
        name->set_halign(Gtk::Align::START);
        name->set_hexpand(true);
        head->append(*name);

        r.status_lbl = Gtk::make_managed<Gtk::Label>();
        if (!cap.available)
        {
            r.status_lbl->set_text("Not available on this computer");
        } else if (!cap.permitted)
        {
            r.status_lbl->set_text("Needs permission");
        } else
        {
            r.status_lbl->set_text("Ready");
        }
        r.status_lbl->add_css_class("dim-label");
        head->append(*r.status_lbl);
        col->append(*head);

        r.detail_lbl = Gtk::make_managed<Gtk::Label>(spec.fallback_note);
        r.detail_lbl->set_wrap(true);
        r.detail_lbl->set_halign(Gtk::Align::START);
        r.detail_lbl->add_css_class("dim-label");
        col->append(*r.detail_lbl);

        r.use_chk = Gtk::make_managed<Gtk::CheckButton>("Show this button");
        r.use_chk->set_active(cap.available && cap.permitted && !cap.command.empty());
        r.use_chk->set_sensitive(cap.available && !cap.command.empty());
        r.use_chk->signal_toggled().connect([this] () { save(nullptr); });
        col->append(*r.use_chk);

        frame->set_child(*col);
        list_box->append(*frame);
        rows.push_back(r);
    }
}

void SessionPage::refresh()
{
    rebuild_rows();

    /* If config already has a command, keep showing the button even if discovery differs */
    auto ini = shell_ini();
    for (auto& r : rows)
    {
        auto existing = wf_shell::ini_get(ini, "panel", r.config_key);
        if (!existing.empty() && r.use_chk)
        {
            r.use_chk->set_active(true);
            if (r.discovered.empty())
            {
                r.discovered = existing;
            }
        }
        if (existing.empty() && r.config_key != "logout_command" && r.use_chk &&
            r.discovered.empty())
        {
            r.use_chk->set_active(false);
        }
    }

    if (status)
    {
        int ready = 0;
        for (const auto& r : rows)
        {
            if (r.use_chk && r.use_chk->get_active())
            {
                ++ready;
            }
        }
        status->set_text("Session: " + std::to_string(ready) +
            " action(s) ready — auto-detected for this system");
    }
}

bool SessionPage::save(std::string *error)
{
    std::map<std::string, std::string> kv;
    for (const auto& r : rows)
    {
        if (r.use_chk && r.use_chk->get_active() && !r.discovered.empty())
        {
            kv[r.config_key] = r.discovered;
        } else
        {
            /* empty = hide / use nothing — panel may still show if defaults hardcode */
            kv[r.config_key] = r.use_chk && r.use_chk->get_active() ? r.discovered : "";
        }
    }

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
        status->set_text("Power & session saved. Logout menu will use these actions.");
    }
    return true;
}

} // namespace wf_settings
