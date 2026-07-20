#pragma once

#include <gtkmm.h>

namespace wf_settings
{

class SoundPage : public Gtk::Box
{
  public:
    SoundPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);

  private:
    void on_apply();
    std::string shell_ini() const;

    Gtk::Entry *play_dev = nullptr;
    Gtk::Entry *capture_dev = nullptr;
    Gtk::DropDown *graph_style = nullptr;
    Gtk::SpinButton *out_ch = nullptr;
    Gtk::CheckButton *prefer_voss = nullptr;
    Gtk::CheckButton *auto_headset = nullptr;
    Gtk::CheckButton *auto_usb = nullptr;
    Gtk::CheckButton *notify_dev = nullptr;
    Gtk::Button *apply_btn = nullptr;
    Gtk::Label *status = nullptr;
};

} // namespace wf_settings
