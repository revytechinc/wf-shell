#include "sound-page.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace wf_settings
{

SoundPage::SoundPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 10)
{
    set_margin(16);
    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Sound</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "Panel volume / Virtual OSS options (wf-shell.ini [panel]). Devices are paths you already use.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(10);
    int r = 0;

    grid->attach(*Gtk::make_managed<Gtk::Label>("Play device"), 0, r, 1, 1);
    play_dev = Gtk::make_managed<Gtk::Entry>();
    play_dev->set_hexpand(true);
    grid->attach(*play_dev, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Capture device"), 0, r, 1, 1);
    capture_dev = Gtk::make_managed<Gtk::Entry>();
    grid->attach(*capture_dev, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Graph style"), 0, r, 1, 1);
    graph_style = Gtk::make_managed<Gtk::DropDown>();
    graph_style->set_model(Gtk::StringList::create({
        "wave-fill", "bars", "line", "none"
    }));
    grid->attach(*graph_style, 1, r++, 1, 1);

    grid->attach(*Gtk::make_managed<Gtk::Label>("Output channels"), 0, r, 1, 1);
    out_ch = Gtk::make_managed<Gtk::SpinButton>();
    out_ch->set_range(1, 16);
    out_ch->set_increments(1, 1);
    grid->attach(*out_ch, 1, r++, 1, 1);

    prefer_voss = Gtk::make_managed<Gtk::CheckButton>("Prefer Virtual OSS");
    grid->attach(*prefer_voss, 0, r++, 2, 1);
    auto_headset = Gtk::make_managed<Gtk::CheckButton>("Auto-switch headset");
    grid->attach(*auto_headset, 0, r++, 2, 1);
    auto_usb = Gtk::make_managed<Gtk::CheckButton>("Auto-switch USB audio");
    grid->attach(*auto_usb, 0, r++, 2, 1);
    notify_dev = Gtk::make_managed<Gtk::CheckButton>("Notify on device change");
    grid->attach(*notify_dev, 0, r++, 2, 1);

    append(*grid);
    apply_btn = Gtk::make_managed<Gtk::Button>("Apply");
    apply_btn->add_css_class("suggested-action");
    apply_btn->set_halign(Gtk::Align::START);
    append(*apply_btn);
    apply_btn->signal_clicked().connect([this] () { on_apply(); });
    refresh();
}

void SoundPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

std::string SoundPage::shell_ini() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

void SoundPage::refresh()
{
    auto ini = shell_ini();
    play_dev->set_text(wf_shell::ini_get(ini, "panel", "volume_play_device"));
    capture_dev->set_text(wf_shell::ini_get(ini, "panel", "volume_capture_device"));
    out_ch->set_value(wf_shell::ini_get_int(ini, "panel", "volume_out_channels", 2));
    prefer_voss->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_prefer_virtual_oss", true));
    auto_headset->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_auto_switch_headset", true));
    auto_usb->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_auto_switch_usb", true));
    notify_dev->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_notify_device_change", true));
    std::string gs = wf_shell::ini_get(ini, "panel", "volume_graph_style");
    const char *styles[] = {"wave-fill", "bars", "line", "none"};
    guint si = 0;
    for (guint i = 0; i < 4; ++i)
    {
        if (gs == styles[i])
        {
            si = i;
            break;
        }
    }
    graph_style->set_selected(si);
}

void SoundPage::on_apply()
{
    auto ini = shell_ini();
    const char *styles[] = {"wave-fill", "bars", "line", "none"};
    auto si = graph_style->get_selected();
    std::map<std::string, std::string> kv;
    kv["volume_play_device"] = play_dev->get_text();
    kv["volume_capture_device"] = capture_dev->get_text();
    kv["volume_out_channels"] = std::to_string(static_cast<int>(out_ch->get_value()));
    kv["volume_graph_style"] = (si < 4) ? styles[si] : "wave-fill";
    kv["volume_prefer_virtual_oss"] = prefer_voss->get_active() ? "true" : "false";
    kv["volume_auto_switch_headset"] = auto_headset->get_active() ? "true" : "false";
    kv["volume_auto_switch_usb"] = auto_usb->get_active() ? "true" : "false";
    kv["volume_notify_device_change"] = notify_dev->get_active() ? "true" : "false";
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
        status->set_text("Sound saved → config.json + legacy ini");
    }
}

} // namespace wf_settings
