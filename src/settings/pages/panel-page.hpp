#pragma once

#include <functional>
#include <gtkmm.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "theme-catalog.hpp"

namespace wf_settings
{

/**
 * Panel page — mother-simple but detailed:
 *  - Theme / edge / thickness as dropdowns & spinners (no raw CSS typing)
 *  - Bar layout as three ordered lists with Add / Up / Down / Remove
 *  - No page-level Save; the one window Save button commits everything.
 */
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
    /**
     * Validate + write look/layout. Returns false if gate refuses.
     * Live UI debounces before calling this.
     */
    bool save(std::string *error = nullptr);

  private:
    void schedule_live_save();
    void rebuild_zone_ui();
    void refill_zone_list(const std::string& zone);
    void set_widgets_from_string(const std::string& left, const std::string& center,
        const std::string& right);
    std::string widgets_string_for(const std::string& zone) const;
    void add_widget_to_zone(const std::string& zone, const std::string& id);
    void remove_selected(const std::string& zone);
    void move_selected(const std::string& zone, int delta);
    void update_bar_preview();
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
    Gtk::Label *bar_preview = nullptr;
    Gtk::Label *info_lbl = nullptr;
    Gtk::Label *status = nullptr;
    std::function<void()> theme_applied_cb;

    std::vector<wf_shell::ThemePack> themes;
    std::vector<std::string> position_values;

    std::unordered_map<std::string, std::vector<std::string>> zone_widgets;
    std::unordered_map<std::string, Gtk::ListBox*> zone_lists;
    std::unordered_map<std::string, Gtk::DropDown*> zone_add_drops;
    std::unordered_map<std::string, std::vector<std::string>> zone_add_ids;

    bool filling = false;
    bool saving = false;
    sigc::connection live_save_debounce;
};

} // namespace wf_settings
