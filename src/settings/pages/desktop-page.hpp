#pragma once

#include <gtkmm.h>
#include <string>
#include <vector>

namespace wf_settings
{

/** Desktop: workspaces + wallpaper in plain language. */
class DesktopPage : public Gtk::Box
{
  public:
    DesktopPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);
    bool save(std::string *error = nullptr);

  private:
    void on_browse();
    void update_preview_hint();
    void refresh_wallpaper_previews();
    std::string wayfire_ini() const;
    std::string shell_ini() const;

    Gtk::SpinButton *vwidth = nullptr;
    Gtk::SpinButton *vheight = nullptr;
    Gtk::Label *ws_summary = nullptr;
    Gtk::Entry *bg_image = nullptr;
    Gtk::DropDown *bg_fill = nullptr;
    Gtk::CheckButton *bg_random = nullptr;
    Gtk::Button *browse_btn = nullptr;
    Gtk::Label *status = nullptr;
    Gtk::Label *fill_hint = nullptr;

    Gtk::FlowBox *wallpaper_flow = nullptr;
    std::vector<std::string> discovered_wallpapers;
    std::vector<Gtk::Button*> wallpaper_buttons;

    std::vector<std::string> fill_values; /* parallel to dropdown labels */
    bool filling = false;
};

} // namespace wf_settings
