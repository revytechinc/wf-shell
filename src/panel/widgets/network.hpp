#pragma once

#include <gtkmm/image.h>
#include <memory>
#include <sigc++/connection.h>
#include <vector>

#include "wf-popover.hpp"
#include "../widget.hpp"
#include "../../util/network/manager.hpp"
#include "network/network-widget.hpp"

/**
 * Panel network tray — icon only (like volume).
 * Click opens the network popover. Tooltip has detail; no IP/SSID on the bar.
 * CSS classes from the active connection colour the icon by activity.
 */
class WayfireNetworkInfo : public WayfireWidget
{
    void on_click();
    void apply_activity_classes(const std::vector<std::string>& classes);
    void show_right_click_menu();
    void run_connection_editor();

    std::vector<sigc::connection> signals;
    std::shared_ptr<NetworkManager> network_manager;
    std::unique_ptr<WayfireMenuWidget> button;
    Gtk::Image icon;
    Gtk::PopoverMenu popover_menu;

    WfOption<std::string> click_command_opt{"panel/network_onclick_command"};
    /* Kept for wcm/ini compatibility; tray is always icon-only. */
    WfOption<bool> no_label{"panel/network_no_label"};

    NetworkControlWidget control;

  public:
    WayfireNetworkInfo();
    ~WayfireNetworkInfo();
    void init(Gtk::Box *container) override;
    void set_connection(std::shared_ptr<Network> network);
};
