#pragma once

/**
 * FreeBSD-native network popover chrome (no NetworkManager).
 * Interface list with green/red state, Wi‑Fi scan/join, Details + traffic.
 *
 * Scan runs only while the network popover is open (no Scan button chrome).
 */

#include "freebsd-network.hpp"
#include "network-types.hpp"
#include "traffic-history.hpp"

#include <atomic>
#include <gtkmm.h>
#include <memory>
#include <string>
#include <vector>

class FreeBSDDetailsWindow;

/** One scanned AP row under a FreeBSD wlan interface. */
class FreeBSDApRow : public Gtk::Box
{
  public:
    FreeBSDApRow(std::string wlan, wf_net::WifiScanEntry entry,
        bool is_connected = false, bool is_saved = false);
    ~FreeBSDApRow() override;
    const wf_net::WifiScanEntry& entry() const { return entry_; }
    bool is_connected() const { return connected_; }
    bool is_saved() const { return saved_; }

    sigc::signal<void(const wf_net::WifiScanEntry&)>& signal_activated()
    {
        return activated_;
    }
    /** Right-click manage actions for known networks. */
    sigc::signal<void(const wf_net::WifiScanEntry&)>& signal_connect_req()
    {
        return connect_req_;
    }
    sigc::signal<void(const wf_net::WifiScanEntry&)>& signal_disconnect_req()
    {
        return disconnect_req_;
    }
    sigc::signal<void(const wf_net::WifiScanEntry&)>& signal_change_password_req()
    {
        return change_password_req_;
    }
    sigc::signal<void(const wf_net::WifiScanEntry&)>& signal_forget_req()
    {
        return forget_req_;
    }

  private:
    void show_manage_menu(double x, double y);

    std::string wlan_;
    wf_net::WifiScanEntry entry_;
    bool connected_ = false;
    bool saved_ = false;
    Gtk::Image icon_;
    Gtk::Image lock_;
    Gtk::Label ssid_;
    Gtk::Label band_;
    Gtk::Label badge_;
    Gtk::Popover menu_;
    Gtk::Box menu_box_{Gtk::Orientation::VERTICAL};
    Gtk::Button btn_connect_;
    Gtk::Button btn_disconnect_;
    Gtk::Button btn_change_pw_;
    Gtk::Button btn_forget_;
    sigc::signal<void(const wf_net::WifiScanEntry&)> activated_;
    sigc::signal<void(const wf_net::WifiScanEntry&)> connect_req_;
    sigc::signal<void(const wf_net::WifiScanEntry&)> disconnect_req_;
    sigc::signal<void(const wf_net::WifiScanEntry&)> change_password_req_;
    sigc::signal<void(const wf_net::WifiScanEntry&)> forget_req_;
    std::vector<sigc::connection> signals_;
};

/** One FreeBSD interface row in the Network popover. */
class FreeBSDIfaceRow : public Gtk::Box
{
  public:
    FreeBSDIfaceRow(std::shared_ptr<FreeBSDNetwork> net,
        wf_net::TrafficCollector *collector);
    ~FreeBSDIfaceRow() override;

    void refresh();
    /** Start Wi‑Fi scan (only while popover open). */
    void on_popover_open();
    void on_popover_close();
    std::shared_ptr<FreeBSDNetwork> network() const { return net_; }

  private:
    void show_context_menu(double x, double y);
    void do_toggle();
    void do_details();
    void do_delete();
    void do_create_wlan();
    void do_scan();
    void do_join(const wf_net::WifiScanEntry& ap);
    void do_disconnect_ap(const wf_net::WifiScanEntry& ap);
    void do_forget_ap(const wf_net::WifiScanEntry& ap);
    void do_change_password_ap(const wf_net::WifiScanEntry& ap);
    void elevate_ifconfig(const char *verb);
    void rebuild_ap_list(const std::vector<wf_net::WifiScanEntry>& aps);
    void set_expanded(bool expanded);
    void toggle_expanded();
    void update_expand_chrome();
    bool is_wifi_clone() const;
    std::string wlan_name() const;

    std::shared_ptr<FreeBSDNetwork> net_;
    wf_net::TrafficCollector *collector_ = nullptr;
    std::vector<sigc::connection> signals_;
    std::unique_ptr<FreeBSDDetailsWindow> details_;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
    bool scanning_ = false;
    bool power_busy_ = false;
    bool popover_open_ = false;
    bool expanded_ = false; /* AP list open for this iface */

    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button btn_expand_;
    Gtk::Image expand_icon_;
    Gtk::Box state_dot_{Gtk::Orientation::HORIZONTAL};
    Gtk::Image icon_;
    Gtk::Label name_lbl_;
    Gtk::Label speed_lbl_;
    Gtk::Label sub_lbl_;
    Gtk::Box text_col_{Gtk::Orientation::VERTICAL};
    Gtk::Box title_row_{Gtk::Orientation::HORIZONTAL};

    /* AP results — shown when expanded_ (per iface, multi-wlan safe) */
    Gtk::Revealer ap_revealer_;
    Gtk::Box ap_box_{Gtk::Orientation::VERTICAL, 2};

    Gtk::Popover menu_;
    Gtk::Box menu_box_{Gtk::Orientation::VERTICAL};
    Gtk::Button btn_toggle_;
    Gtk::Button btn_details_;
    Gtk::Button btn_delete_;
};

/** Details window: properties + line traffic graph (5 min history). */
class FreeBSDDetailsWindow : public Gtk::Window
{
  public:
    FreeBSDDetailsWindow(std::shared_ptr<FreeBSDNetwork> net,
        wf_net::TrafficCollector *collector);
    ~FreeBSDDetailsWindow() override;

    /**
     * Show the window. Never set_transient_for a layer-shell panel (breaks
     * close). Optionally inherits the panel's Gdk::Display for Wayland.
     */
    void present_for(Gtk::Window *display_source = nullptr);

    /** Stop tick, unmap, then emit closed on idle (safe destroy by owner). */
    void request_close();

    sigc::signal<void()>& signal_closed() { return closed_; }

  private:
    void rebuild_props();
    void update_traffic_meta();
    void on_graph_style_changed();
    bool on_tick();
    void ensure_tick();
    void stop_tick();
    void attach_to_app();
    void draw_graph(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);

    std::shared_ptr<FreeBSDNetwork> net_;
    wf_net::TrafficCollector *collector_ = nullptr;
    std::string graph_style_ = "wave-fill";
    std::string props_fp_;
    bool closing_ = false;

    Gtk::Box root_{Gtk::Orientation::VERTICAL};
    Gtk::Grid props_;
    Gtk::Label traffic_meta_;
    Gtk::ComboBoxText graph_combo_;
    Gtk::DrawingArea graph_;
    Gtk::Button close_btn_;
    sigc::connection tick_;
    sigc::connection close_idle_;
    std::vector<sigc::connection> signals_;
    sigc::signal<void()> closed_;
};
