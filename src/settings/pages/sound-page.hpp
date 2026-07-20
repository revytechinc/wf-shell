#pragma once

#include <gtkmm.h>
#include <string>
#include <vector>

namespace wf_settings
{

/**
 * Sound — mother-simple, detailed:
 * devices from live discovery as dropdowns (never type /dev/dspN),
 * graph style as plain English, toggles for auto-switch.
 */
class SoundPage : public Gtk::Box
{
  public:
    SoundPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);
    bool save(std::string *error = nullptr);

  private:
    void refill_device_drops();
    std::string shell_ini() const;
    std::string selected_path(Gtk::DropDown *drop,
        const std::vector<std::string>& paths) const;

    Gtk::DropDown *play_drop = nullptr;
    Gtk::DropDown *capture_drop = nullptr;
    Gtk::DropDown *graph_style = nullptr;
    Gtk::SpinButton *out_ch = nullptr;
    Gtk::CheckButton *prefer_voss = nullptr;
    Gtk::CheckButton *auto_headset = nullptr;
    Gtk::CheckButton *auto_usb = nullptr;
    Gtk::CheckButton *notify_dev = nullptr;
    Gtk::Label *status = nullptr;
    Gtk::Label *help_lbl = nullptr;

    std::vector<std::string> play_paths;
    std::vector<std::string> capture_paths;
    bool filling = false;
};

} // namespace wf_settings
