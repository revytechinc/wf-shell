#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm.h>
#include <sigc++/connection.h>

#include "gtkmm/overlay.h"
#include "manager.hpp"
#include "traffic-history.hpp"
#include "vpn.hpp"
#include "wifi-ap.hpp"

class FreeBSDIfaceRow;

class AccessPointWidget : public Gtk::Box
{
  private:
    Gtk::Overlay overlay;
    Gtk::Image wifi, security;
    Gtk::Label label, band;
    std::shared_ptr<AccessPoint> ap;
    std::string path;

  public:
    std::vector<sigc::connection> signals;
    AccessPointWidget(std::string path, std::shared_ptr<AccessPoint> ap);
    ~AccessPointWidget();
    std::shared_ptr<AccessPoint> get_ap();
};

class DeviceControlWidget : public Gtk::Box
{
  private:
    std::map<std::string, std::shared_ptr<AccessPointWidget>> access_points;
    std::shared_ptr<Network> network;
    Gtk::Label label;
    Gtk::Image image;
    Gtk::Revealer revealer;
    Gtk::Box revealer_box, topbox;
    std::vector<sigc::connection> signals;

  public:
    DeviceControlWidget(std::shared_ptr<Network> network);
    ~DeviceControlWidget();
    void add_access_point(std::shared_ptr<AccessPoint> ap);
    void remove_access_point(std::string path);
    void selected_access_point(std::string path, std::shared_ptr<AccessPointWidget> widget);
    void sort_access_points();
    std::string type;
};

class VPNControlWidget : public Gtk::Box
{
  private:
    std::shared_ptr<VpnConfig> config;
    Gtk::Image image;
    Gtk::Label label;

  public:
    std::vector<sigc::connection> signals;

    VPNControlWidget(std::shared_ptr<VpnConfig> config);
    ~VPNControlWidget();
};

/**
 * Network popover body.
 * FreeBSD: interface list only (no NetworkManager chrome).
 * Linux: NetworkManager toggles, VPN, modem, NM-not-running message.
 */
class NetworkControlWidget : public Gtk::Box
{
    /* Linux-only NM chrome (never shown on FreeBSD) */
    Gtk::Label network_manager_failed;
    Gtk::Box top;
    Gtk::CheckButton global_networking, wifi_networking, mobile_networking;
    sigc::connection signal_network, signal_wifi, signal_mobile;

    Gtk::Box wire_box, wifi_box, mobile_box, vpn_box, bt_box;
    std::map<std::string, std::shared_ptr<DeviceControlWidget>> widgets;
    std::map<std::string, std::shared_ptr<FreeBSDIfaceRow>> freebsd_rows;
    std::map<std::string, std::shared_ptr<VPNControlWidget>> vpn_widgets;
    std::vector<sigc::connection> signals;
    bool freebsd_mode = false;
    std::unique_ptr<wf_net::TrafficCollector> traffic_;

    void setup_freebsd_ui();
    void setup_linux_ui();
    void seed_devices();
    void freebsd_sync_collector();

  public:
    NetworkControlWidget();
    ~NetworkControlWidget();
    std::shared_ptr<NetworkManager> network_manager;
    void update_globals();
    void add_device(std::shared_ptr<Network> network);
    void remove_device(std::shared_ptr<Network> network);
    void add_vpn(std::shared_ptr<VpnConfig> config);
    void remove_vpn(std::string path);
    void nm_start();
    void nm_stop();
    void mm_start();
    void mm_stop();
    /** FreeBSD: scan Wi‑Fi only while the tray popover is open. */
    void on_popover_open();
    void on_popover_close();
};
