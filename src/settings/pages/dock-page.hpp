#pragma once

#include <gtkmm.h>

namespace wf_settings
{

class DockPage : public Gtk::Box
{
  public:
    DockPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);
    bool save(std::string *error = nullptr);

  private:
    std::string shell_ini() const;

    Gtk::DropDown *position = nullptr;
    Gtk::SpinButton *icon_h = nullptr;
    Gtk::SpinButton *dock_h = nullptr;
    Gtk::CheckButton *autohide = nullptr;
    Gtk::Label *status = nullptr;
    bool filling = false;
};

} // namespace wf_settings
