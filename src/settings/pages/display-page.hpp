#pragma once

#include <gtkmm.h>
#include <string>
#include <utility>
#include <vector>

#include "display-config.hpp"

namespace wf_settings
{

/**
 * Display page: auto-discover outputs/modes via wlr-randr.
 *
 * Resolution and refresh are separate dropdowns, but tightly coupled:
 * changing resolution repopulates only valid refresh rates for that WxH.
 * Apply resolves the pair against advertised modes and refuses inventing.
 */
class DisplayPage : public Gtk::Box
{
  public:
    DisplayPage();

    void refresh();
    void set_status_target(Gtk::Label *status_label);
    bool save(std::string *error = nullptr);

  private:
    void on_output_changed();
    void on_resolution_changed();
    void fill_resolutions();
    void fill_refresh_rates();
    void update_info();
    void update_apply_sensitive();
    void update_layout_visualization();
    /** Resolve current UI selection to a supported mode (or invalid). */
    wf_shell::DisplayMode selected_safe_mode() const;
    const wf_shell::DisplayOutput *selected_output() const;
    std::string wayfire_ini_path() const;
    std::string kanshi_path() const;

    Gtk::DropDown *output_drop = nullptr;
    Gtk::DropDown *res_drop    = nullptr;
    Gtk::DropDown *rate_drop   = nullptr;
    Gtk::Label *info_lbl = nullptr;
    Gtk::Button *refresh_btn = nullptr; /* discover only */
    Gtk::Button *use_mode_btn = nullptr; /* intentional modeset operation */
    Gtk::Label *status = nullptr;

    Gtk::Box *layout_container = nullptr;
    Gtk::Fixed *layout_fixed = nullptr;
    std::vector<Gtk::Button*> monitor_buttons;

    wf_shell::DisplayProbeResult probe;
    /* For selected output: unique resolutions in dropdown order */
    std::vector<std::pair<int, int>> res_cache;
    /* Refresh rates for currently selected resolution */
    std::vector<double> rate_cache;

    bool filling_ui = false;
    sigc::connection res_conn;
    sigc::connection rate_conn;
    sigc::connection out_conn;
};

} // namespace wf_settings
