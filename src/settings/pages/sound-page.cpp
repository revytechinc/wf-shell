#include "sound-page.hpp"

#include "audio/audio-backend.hpp"
#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <cstdlib>
#include <map>
#include <string>

namespace wf_settings
{
namespace
{

const char *kGraphRaw[] = {"wave-fill", "bars", "line", "none"};
const char *kGraphLabel[] = {
    "Filled wave", "Bars", "Line", "Hidden (no graph)"
};

void select_path(Gtk::DropDown *drop, const std::vector<std::string>& paths,
    const std::string& want)
{
    if (!drop || paths.empty())
    {
        return;
    }
    guint sel = 0;
    for (guint i = 0; i < paths.size(); ++i)
    {
        if (paths[i] == want)
        {
            sel = i;
            break;
        }
    }
    /* If current path not in list, keep first (usually “Default”) */
    if (!want.empty())
    {
        bool found = false;
        for (const auto& p : paths)
        {
            if (p == want)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            /* Append unknown current as last option so we do not wipe it */
            /* Caller should have already included it — just pick 0. */
        }
    }
    drop->set_selected(sel);
}

} // namespace

SoundPage::SoundPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 16)
{
    set_margin(20);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<span size='large'><b>Sound</b></span>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    help_lbl = Gtk::make_managed<Gtk::Label>(
        "Pick speakers and microphone from the list. "
        "These control what the panel volume widget uses.");
    help_lbl->set_wrap(true);
    help_lbl->add_css_class("dim-label");
    help_lbl->set_halign(Gtk::Align::START);
    append(*help_lbl);

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

    lab("Speakers / output");
    play_drop = Gtk::make_managed<Gtk::DropDown>();
    play_drop->set_hexpand(true);
    play_drop->set_tooltip_text("Where sound plays");
    grid->attach(*play_drop, 1, r++, 1, 1);

    lab("Microphone / input");
    capture_drop = Gtk::make_managed<Gtk::DropDown>();
    capture_drop->set_hexpand(true);
    capture_drop->set_tooltip_text("Where the mic listens");
    grid->attach(*capture_drop, 1, r++, 1, 1);

    lab("Volume graph style");
    graph_style = Gtk::make_managed<Gtk::DropDown>();
    {
        std::vector<Glib::ustring> labels;
        for (auto *s : kGraphLabel)
        {
            labels.emplace_back(s);
        }
        graph_style->set_model(Gtk::StringList::create(labels));
    }
    grid->attach(*graph_style, 1, r++, 1, 1);

    lab("Output channels");
    out_ch = Gtk::make_managed<Gtk::SpinButton>();
    out_ch->set_range(1, 16);
    out_ch->set_increments(1, 1);
    out_ch->set_tooltip_text("Usually 2 for stereo");
    grid->attach(*out_ch, 1, r++, 1, 1);

    prefer_voss = Gtk::make_managed<Gtk::CheckButton>(
        "Prefer Virtual OSS when it is available");
    grid->attach(*prefer_voss, 0, r++, 2, 1);
    auto_headset = Gtk::make_managed<Gtk::CheckButton>(
        "Switch automatically when I plug in a headset");
    grid->attach(*auto_headset, 0, r++, 2, 1);
    auto_usb = Gtk::make_managed<Gtk::CheckButton>(
        "Switch automatically for USB audio devices");
    grid->attach(*auto_usb, 0, r++, 2, 1);
    notify_dev = Gtk::make_managed<Gtk::CheckButton>(
        "Show a notice when the audio device changes");
    grid->attach(*notify_dev, 0, r++, 2, 1);

    append(*grid);

    auto live = [this] () {
        if (!filling)
        {
            save(nullptr);
        }
    };
    play_drop->property_selected().signal_changed().connect(live);
    capture_drop->property_selected().signal_changed().connect(live);
    graph_style->property_selected().signal_changed().connect(live);
    out_ch->signal_value_changed().connect(live);
    prefer_voss->signal_toggled().connect(live);
    auto_headset->signal_toggled().connect(live);
    auto_usb->signal_toggled().connect(live);
    notify_dev->signal_toggled().connect(live);

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

std::string SoundPage::selected_path(Gtk::DropDown *drop,
    const std::vector<std::string>& paths) const
{
    if (!drop || paths.empty())
    {
        return {};
    }
    auto idx = drop->get_selected();
    if (idx == GTK_INVALID_LIST_POSITION || idx >= paths.size())
    {
        return {};
    }
    return paths[idx];
}

void SoundPage::refill_device_drops()
{
    play_paths.clear();
    capture_paths.clear();
    std::vector<Glib::ustring> play_labels;
    std::vector<Glib::ustring> cap_labels;

    /* Default empty = let the stack choose */
    play_paths.push_back("");
    play_labels.emplace_back("Default (system choice)");
    capture_paths.push_back("");
    cap_labels.emplace_back("Default (system choice)");

    try
    {
        auto backend = wf_audio::AudioBackendFactory::create();
        if (backend)
        {
            for (const auto& d : backend->list_playback_devices())
            {
                if (d.path.empty() && d.id.empty())
                {
                    continue;
                }
                std::string path = !d.path.empty() ? d.path : d.id;
                std::string label = d.description.empty() ? path : d.description;
                if (!d.kind.empty())
                {
                    label += "  (" + d.kind + ")";
                }
                if (d.is_default)
                {
                    label += "  · current";
                }
                play_paths.push_back(path);
                play_labels.push_back(label);
            }
            for (const auto& d : backend->list_capture_devices())
            {
                if (d.path.empty() && d.id.empty())
                {
                    continue;
                }
                std::string path = !d.path.empty() ? d.path : d.id;
                std::string label = d.description.empty() ? path : d.description;
                if (!d.kind.empty())
                {
                    label += "  (" + d.kind + ")";
                }
                if (d.is_default)
                {
                    label += "  · current";
                }
                capture_paths.push_back(path);
                cap_labels.push_back(label);
            }
        }
    } catch (...)
    {
        /* Fail-soft: dropdowns still have Default */
    }

    play_drop->set_model(Gtk::StringList::create(play_labels));
    capture_drop->set_model(Gtk::StringList::create(cap_labels));
}

void SoundPage::refresh()
{
    filling = true;
    auto ini = shell_ini();
    refill_device_drops();

    auto play = wf_shell::ini_get(ini, "panel", "volume_play_device");
    auto cap  = wf_shell::ini_get(ini, "panel", "volume_capture_device");

    /* If saved path missing from discovery, append so we do not lose it */
    auto ensure = [] (std::vector<std::string>& paths,
        std::vector<Glib::ustring>& /*labels*/, Gtk::DropDown *drop,
        const std::string& path) {
        if (path.empty())
        {
            return;
        }
        for (const auto& p : paths)
        {
            if (p == path)
            {
                return;
            }
        }
        paths.push_back(path);
        /* rebuild model is awkward mid-flight — append via re-set below */
        (void)drop;
    };
    bool need_rebuild = false;
    auto has = [] (const std::vector<std::string>& v, const std::string& p) {
        for (const auto& x : v)
        {
            if (x == p)
            {
                return true;
            }
        }
        return false;
    };
    if (!play.empty() && !has(play_paths, play))
    {
        play_paths.push_back(play);
        need_rebuild = true;
    }
    if (!cap.empty() && !has(capture_paths, cap))
    {
        capture_paths.push_back(cap);
        need_rebuild = true;
    }
    if (need_rebuild)
    {
        std::vector<Glib::ustring> pl, cl;
        for (const auto& p : play_paths)
        {
            pl.push_back(p.empty() ? Glib::ustring("Default (system choice)") :
                Glib::ustring(p));
        }
        for (const auto& p : capture_paths)
        {
            cl.push_back(p.empty() ? Glib::ustring("Default (system choice)") :
                Glib::ustring(p));
        }
        /* Prefer friendly labels for known; path fallback for orphans */
        play_drop->set_model(Gtk::StringList::create(pl));
        capture_drop->set_model(Gtk::StringList::create(cl));
    }
    (void)ensure;

    select_path(play_drop, play_paths, play);
    select_path(capture_drop, capture_paths, cap);

    out_ch->set_value(wf_shell::ini_get_int(ini, "panel", "volume_out_channels", 2));
    prefer_voss->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_prefer_virtual_oss", true));
    auto_headset->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_auto_switch_headset", true));
    auto_usb->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_auto_switch_usb", true));
    notify_dev->set_active(wf_shell::ini_get_bool(ini, "panel", "volume_notify_device_change", true));

    std::string gs = wf_shell::ini_get(ini, "panel", "volume_graph_style");
    guint si = 0;
    for (guint i = 0; i < 4; ++i)
    {
        if (gs == kGraphRaw[i])
        {
            si = i;
            break;
        }
    }
    graph_style->set_selected(si);
    filling = false;
    if (status)
    {
        status->set_text("");
    }
}

bool SoundPage::save(std::string *error)
{
    auto si = graph_style->get_selected();
    std::map<std::string, std::string> kv;
    kv["volume_play_device"] = selected_path(play_drop, play_paths);
    kv["volume_capture_device"] = selected_path(capture_drop, capture_paths);
    kv["volume_out_channels"] = std::to_string(static_cast<int>(out_ch->get_value()));
    kv["volume_graph_style"] = (si < 4) ? kGraphRaw[si] : "wave-fill";
    kv["volume_prefer_virtual_oss"] = prefer_voss->get_active() ? "true" : "false";
    kv["volume_auto_switch_headset"] = auto_headset->get_active() ? "true" : "false";
    kv["volume_auto_switch_usb"] = auto_usb->get_active() ? "true" : "false";
    kv["volume_notify_device_change"] = notify_dev->get_active() ? "true" : "false";
    std::string err;
    if (!wf_shell::settings_save_section("panel", kv, &err))
    {
        if (status)
        {
            status->set_text("We couldn't update the sound settings: " + err);
        }
        return false;
    }
    if (status)
    {
        status->set_text("✨ Sound settings updated successfully!");
    }
    return true;
}

} // namespace wf_settings
