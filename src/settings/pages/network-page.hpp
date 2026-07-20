#pragma once

#include <gtkmm.h>

namespace wf_settings
{

class NetworkPage : public Gtk::Box
{
  public:
    NetworkPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);

  private:
    void on_apply();
    std::string shell_ini() const;

    Gtk::SpinButton *icon_size = nullptr;
    Gtk::DropDown *net_status = nullptr;
    Gtk::CheckButton *invert_icon = nullptr;
    Gtk::CheckButton *use_color = nullptr;
    Gtk::CheckButton *no_label = nullptr;
    Gtk::Entry *onclick = nullptr;
    Gtk::Button *apply_btn = nullptr;
    Gtk::Label *status = nullptr;
};

} // namespace wf_settings
