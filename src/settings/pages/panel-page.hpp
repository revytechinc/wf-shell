#pragma once

#include <functional>
#include <gtkmm.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "theme-catalog.hpp"

namespace wf_settings
{

/** Panel: theme, edge, widgets as checklists — not free-form widget strings. */
class PanelPage : public Gtk::Box
{
  public:
    PanelPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);
    void set_theme_applied_callback(std::function<void()> cb)
    {
        theme_applied_cb = std::move(cb);
    }

  private:
    void on_apply();
    void rebuild_widget_checks();
    void set_widgets_from_string(const std::string& left, const std::string& center,
        const std::string& right);
    std::string widgets_string_for(const std::string& zone) const;
    std::string shell_ini_path() const;
    std::string resource_themes_dir() const;
    std::string user_themes_dir() const;

    Gtk::DropDown *theme_drop = nullptr;
    Gtk::DropDown *position_drop = nullptr;
    Gtk::CheckButton *autohide_chk = nullptr;
    Gtk::CheckButton *menu_list_chk = nullptr;
    Gtk::CheckButton *menu_cats_chk = nullptr;
    Gtk::SpinButton *height_spin = nullptr;
    Gtk::Box *widget_cols = nullptr;
    Gtk::Label *info_lbl = nullptr;
    Gtk::Button *refresh_btn = nullptr;
    Gtk::Button *apply_btn = nullptr;
    Gtk::Label *status = nullptr;
    std::function<void()> theme_applied_cb;

    std::vector<wf_shell::ThemePack> themes;
    std::vector<std::string> position_values;
    /** zone ("left"|"center"|"right") + widget id → checkbutton */
    std::unordered_map<std::string, Gtk::CheckButton*> widget_checks;
    bool filling = false;
};

} // namespace wf_settings
