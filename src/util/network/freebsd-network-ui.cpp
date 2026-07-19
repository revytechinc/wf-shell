#include "freebsd-network-ui.hpp"

#include "network-info.hpp"
#include "network-log.hpp"
#include "network-types.hpp"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glib.h>
#include <glibmm/main.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <unistd.h>

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

namespace
{

void run_elevated(const std::string& cmdline)
{
    /* Prefer doas -n, then sudo -n, then plain (works if root). */
    const char *try_cmds[] = {
        "doas -n ",
        "sudo -n ",
        "",
        nullptr,
    };
    for (int i = 0; try_cmds[i]; ++i)
    {
        std::string full = std::string(try_cmds[i]) + cmdline;
        int st = system(full.c_str());
        if (st == 0)
        {
            return;
        }
    }
}

/**
 * Marshal work back to the GTK main loop from a worker thread.
 * GLib idle sources are thread-safe to add; GTK widgets are not.
 */
template<typename F>
void ui_idle(F&& fn)
{
    auto *heap = new std::function<void()>(std::forward<F>(fn));
    g_idle_add(
        [] (gpointer data) -> gboolean {
            auto *f = static_cast<std::function<void()>*>(data);
            try
            {
                (*f)();
            } catch (...)
            {
                /* never break the main loop */
            }
            delete f;
            return G_SOURCE_REMOVE;
        },
        heap);
}

std::string get_graph_style_cache_path()
{
    const char *home = getenv("HOME");
    if (home)
    {
        return std::string(home) + "/.config/wf-shell/graph_style.txt";
    }
    return "";
}

std::string load_persisted_graph_style()
{
    std::string path = get_graph_style_cache_path();
    if (!path.empty())
    {
        std::ifstream in(path);
        if (in)
        {
            std::string style;
            if (std::getline(in, style))
            {
                // Strip whitespace
                style.erase(style.find_last_not_of(" \n\r\t") + 1);
                style.erase(0, style.find_first_not_of(" \n\r\t"));
                return style;
            }
        }
    }
    return "wave-fill"; // default
}

void save_persisted_graph_style(const std::string& style)
{
    std::string path = get_graph_style_cache_path();
    if (!path.empty())
    {
        std::ofstream out(path);
        if (out)
        {
            out << style << std::endl;
        }
    }
}

} // namespace

/* ─── FreeBSDApRow ───────────────────────────────────────────────────────── */

FreeBSDApRow::FreeBSDApRow(std::string wlan, wf_net::WifiScanEntry entry,
    bool is_connected, bool is_saved) :
    Gtk::Box(Gtk::Orientation::HORIZONTAL, 8),
    wlan_(std::move(wlan)),
    entry_(std::move(entry)),
    connected_(is_connected),
    saved_(is_saved)
{
    add_css_class("access-point");
    if (connected_)
    {
        add_css_class("active");
        add_css_class("connected");
    }
    if (saved_ && !connected_)
    {
        add_css_class("saved");
    }
    if (entry_.security != "open")
    {
        add_css_class("secure");
    } else
    {
        add_css_class("insecure");
    }
    set_hexpand(true);
    set_margin_start(8);
    set_margin_top(1);
    set_margin_bottom(1);

    const auto pct = entry_.signal_dbm < 0 ?
        wf_net::wifi_signal_to_percent(entry_.signal_dbm) :
        (entry_.signal_dbm > 0 ? static_cast<unsigned char>(
            std::min(100, entry_.signal_dbm)) : 50);
    icon_.set_from_icon_name(wf_net::wifi_signal_icon_base(pct) + "-symbolic");
    icon_.set_pixel_size(22);
    icon_.add_css_class("access-point");
    if (entry_.security != "open")
    {
        lock_.set_from_icon_name("network-wireless-encrypted-symbolic");
        lock_.set_pixel_size(14);
        lock_.add_css_class("security");
    }

    ssid_.set_text(entry_.ssid);
    ssid_.set_halign(Gtk::Align::START);
    ssid_.set_hexpand(true);
    /* Prefer beacon-IE generation (Wi‑Fi 7 from EHT) over band-only heuristic. */
    auto band = wf_net::format_wifi_radio_label(entry_);
    if (band.empty())
    {
        band = wf_net::format_wifi_band(entry_.freq_mhz);
    }
    band_.set_text(band);
    band_.add_css_class("band");
    band_.set_halign(Gtk::Align::END);
    if (!entry_.generation.empty())
    {
        band_.set_tooltip_text(entry_.generation +
            (entry_.phy.eht ? " (802.11be EHT)" :
             entry_.phy.he  ? " (802.11ax HE)" :
             entry_.phy.vht ? " (802.11ac VHT)" : ""));
    }

    if (connected_)
    {
        badge_.set_text("Connected");
        badge_.add_css_class("ap-connected-badge");
        badge_.set_halign(Gtk::Align::END);
    } else if (saved_)
    {
        badge_.set_text("Saved");
        badge_.add_css_class("ap-saved-badge");
        badge_.set_halign(Gtk::Align::END);
        set_tooltip_text("Password saved — click to connect · right-click to manage");
    }

    append(icon_);
    if (entry_.security != "open")
    {
        append(lock_);
    }
    append(ssid_);
    if (connected_ || saved_)
    {
        append(badge_);
    }
    append(band_);

    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    signals_.push_back(click->signal_released().connect(
        [this] (int, double, double) {
            if (connected_)
            {
                return; /* already on this network (this iface) */
            }
            activated_.emit(entry_);
        }));
    add_controller(click);

    /* Manage menu for known (saved) and currently connected networks. */
    if (saved_ || connected_)
    {
        menu_box_.set_spacing(2);
        btn_connect_.set_label("Connect");
        btn_disconnect_.set_label("Disconnect");
        btn_change_pw_.set_label("Change password…");
        btn_forget_.set_label("Forget network");
        btn_forget_.add_css_class("destructive-action");
        for (auto *b : {&btn_connect_, &btn_disconnect_, &btn_change_pw_,
            &btn_forget_})
        {
            b->set_halign(Gtk::Align::FILL);
            menu_box_.append(*b);
        }
        menu_.set_parent(*this);
        menu_.set_child(menu_box_);
        menu_.set_has_arrow(false);

        btn_connect_.set_visible(!connected_ && saved_);
        btn_disconnect_.set_visible(connected_);
        btn_change_pw_.set_visible(entry_.security != "open");
        btn_forget_.set_visible(true);

        signals_.push_back(btn_connect_.signal_clicked().connect([this] () {
            menu_.popdown();
            connect_req_.emit(entry_);
        }));
        signals_.push_back(btn_disconnect_.signal_clicked().connect([this] () {
            menu_.popdown();
            disconnect_req_.emit(entry_);
        }));
        signals_.push_back(btn_change_pw_.signal_clicked().connect([this] () {
            menu_.popdown();
            change_password_req_.emit(entry_);
        }));
        signals_.push_back(btn_forget_.signal_clicked().connect([this] () {
            menu_.popdown();
            forget_req_.emit(entry_);
        }));

        auto rclick = Gtk::GestureClick::create();
        rclick->set_button(3);
        signals_.push_back(rclick->signal_pressed().connect(
            [this, rclick] (int, double x, double y) {
                rclick->set_state(Gtk::EventSequenceState::CLAIMED);
                show_manage_menu(x, y);
            }));
        add_controller(rclick);
    }
}

FreeBSDApRow::~FreeBSDApRow()
{
    for (auto& s : signals_)
    {
        s.disconnect();
    }
    menu_.popdown();
    menu_.unparent();
}

void FreeBSDApRow::show_manage_menu(double x, double y)
{
    btn_connect_.set_visible(!connected_ && saved_);
    btn_disconnect_.set_visible(connected_);
    btn_change_pw_.set_visible(entry_.security != "open");
    Gdk::Rectangle r;
    r.set_x(static_cast<int>(x));
    r.set_y(static_cast<int>(y));
    r.set_width(1);
    r.set_height(1);
    menu_.set_pointing_to(r);
    menu_.popup();
}

/* ─── FreeBSDIfaceRow ────────────────────────────────────────────────────── */

FreeBSDIfaceRow::FreeBSDIfaceRow(std::shared_ptr<FreeBSDNetwork> net,
    wf_net::TrafficCollector *collector) :
    Gtk::Box(Gtk::Orientation::VERTICAL, 2),
    net_(std::move(net)),
    collector_(collector)
{
    add_css_class("freebsd-iface");
    add_css_class("device");
    set_hexpand(true);
    set_margin_top(2);
    set_margin_bottom(2);
    set_margin_start(4);
    set_margin_end(4);

    state_dot_.set_size_request(10, 10);
    state_dot_.set_valign(Gtk::Align::CENTER);
    state_dot_.add_css_class("state-dot");
    icon_.set_pixel_size(22);
    icon_.set_valign(Gtk::Align::CENTER);
    name_lbl_.set_halign(Gtk::Align::START);
    name_lbl_.set_hexpand(true);
    speed_lbl_.set_halign(Gtk::Align::END);
    speed_lbl_.add_css_class("speed");
    sub_lbl_.set_halign(Gtk::Align::START);
    sub_lbl_.add_css_class("sub");
    sub_lbl_.set_wrap(true);

    title_row_.set_hexpand(true);
    title_row_.append(name_lbl_);
    title_row_.append(speed_lbl_);
    text_col_.set_hexpand(true);
    text_col_.append(title_row_);
    text_col_.append(sub_lbl_);

    /* Expand/collapse chevron — Wi‑Fi AP list (and useful for multi-wlan) */
    expand_icon_.set_from_icon_name("pan-end-symbolic");
    expand_icon_.set_pixel_size(16);
    btn_expand_.set_child(expand_icon_);
    btn_expand_.add_css_class("flat");
    btn_expand_.add_css_class("iface-expand");
    btn_expand_.set_valign(Gtk::Align::CENTER);
    btn_expand_.set_tooltip_text("Show networks");
    btn_expand_.set_visible(false);

    header_.set_hexpand(true);
    header_.append(btn_expand_);
    header_.append(state_dot_);
    header_.append(icon_);
    header_.append(text_col_);
    append(header_);

    ap_box_.set_margin_start(18);
    ap_revealer_.set_child(ap_box_);
    ap_revealer_.set_reveal_child(false);
    ap_revealer_.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    append(ap_revealer_);

    /* Context menu: power + details (not expand) */
    menu_box_.set_spacing(2);
    btn_toggle_.set_label("Turn off");
    btn_details_.set_label("Details…");
    btn_delete_.set_label("Delete interface…");
    btn_delete_.add_css_class("destructive-action");
    for (auto *b : {&btn_toggle_, &btn_details_, &btn_delete_})
    {
        b->set_halign(Gtk::Align::FILL);
        menu_box_.append(*b);
    }
    menu_.set_parent(*this);
    menu_.set_child(menu_box_);
    menu_.set_has_arrow(false);

    auto rclick = Gtk::GestureClick::create();
    rclick->set_button(3);
    signals_.push_back(rclick->signal_pressed().connect(
        [this, rclick] (int, double x, double y) {
            rclick->set_state(Gtk::EventSequenceState::CLAIMED);
            show_context_menu(x, y);
        }));
    header_.add_controller(rclick);

    /*
     * Left-click header body: expand/collapse Wi‑Fi list (or power for non-wifi).
     * Chevron has its own handler.
     */
    signals_.push_back(btn_expand_.signal_clicked().connect(
        [this] () { toggle_expanded(); }));

    auto lclick = Gtk::GestureClick::create();
    lclick->set_button(1);
    signals_.push_back(lclick->signal_released().connect(
        [this] (int, double, double) {
            if (is_wifi_clone() && net_ && net_->info().up)
            {
                toggle_expanded();
            } else if (!is_wifi_clone())
            {
                do_toggle();
            } else
            {
                do_toggle(); /* wifi down: turn on */
            }
        }));
    /* Only on text/icon — not the whole header (chevron has its own button) */
    text_col_.add_controller(lclick);

    signals_.push_back(btn_toggle_.signal_clicked().connect([this] () {
        menu_.popdown();
        do_toggle();
    }));
    signals_.push_back(btn_details_.signal_clicked().connect([this] () {
        menu_.popdown();
        do_details();
    }));
    signals_.push_back(btn_delete_.signal_clicked().connect([this] () {
        menu_.popdown();
        do_delete();
    }));

    signals_.push_back(net_->signal_network_altered().connect([this] () {
        refresh();
    }));

    if (collector_ && wf_net::is_valid_traffic_ifname(net_->get_interface()) &&
        net_->info().wifi_role != wf_net::WifiRole::ParentRadio)
    {
        collector_->watch(net_->get_interface());
    }

    refresh();
}

FreeBSDIfaceRow::~FreeBSDIfaceRow()
{
    if (alive_)
    {
        alive_->store(false);
    }
    for (auto& s : signals_)
    {
        s.disconnect();
    }
    details_.reset();
    menu_.popdown();
    menu_.unparent();
}

void FreeBSDIfaceRow::on_popover_open()
{
    popover_open_ = true;
    /*
     * Always expand Wi‑Fi when the popover opens so the full scan list is
     * available (not just the connected SSID on the header). Collapse is
     * still available via the chevron.
     */
    if (is_wifi_clone() && net_ && net_->info().up)
    {
        set_expanded(true);
        do_scan(); /* always refresh neighborhood — never reuse a 1-row stale list */
    } else
    {
        set_expanded(false);
    }
    refresh();
}

void FreeBSDIfaceRow::on_popover_close()
{
    popover_open_ = false;
    set_expanded(false);
}

void FreeBSDIfaceRow::update_expand_chrome()
{
    const bool can_expand = is_wifi_clone() && net_ && net_->info().up;
    btn_expand_.set_visible(can_expand);
    if (!can_expand)
    {
        expand_icon_.set_from_icon_name("pan-end-symbolic");
        btn_expand_.set_tooltip_text("");
        return;
    }
    if (expanded_)
    {
        expand_icon_.set_from_icon_name("pan-down-symbolic");
        btn_expand_.set_tooltip_text("Hide networks");
    } else
    {
        expand_icon_.set_from_icon_name("pan-end-symbolic");
        btn_expand_.set_tooltip_text("Show networks");
    }
}

void FreeBSDIfaceRow::set_expanded(bool expanded)
{
    if (!is_wifi_clone() || !net_ || !net_->info().up)
    {
        expanded_ = false;
    } else
    {
        expanded_ = expanded;
    }
    ap_revealer_.set_reveal_child(expanded_ && popover_open_);
    update_expand_chrome();
    if (expanded_ && popover_open_ && is_wifi_clone() && net_->info().up)
    {
        /* Rescan every expand — a prior partial scan can leave only the current AP. */
        if (!scanning_)
        {
            do_scan();
        }
    }
    wf_net::net_event_info("wifi.ui.expand", {
        wf_net::field_bool("expanded", expanded_),
        wf_net::field_str("wlan", wlan_name()),
    }, wlan_name());
}

void FreeBSDIfaceRow::toggle_expanded()
{
    set_expanded(!expanded_);
}

bool FreeBSDIfaceRow::is_wifi_clone() const
{
    return net_ && (net_->info().wifi_role == wf_net::WifiRole::WlanClone ||
        wf_net::is_wlan_clone_name(net_->get_interface()));
}

std::string FreeBSDIfaceRow::wlan_name() const
{
    return net_ ? net_->get_interface() : std::string{};
}

void FreeBSDIfaceRow::refresh()
{
    if (!net_)
    {
        return;
    }
    const auto& info = net_->info();
    const bool up = info.up;
    const bool parent_only =
        info.wifi_role == wf_net::WifiRole::ParentRadio && info.wifi_needs_clone;
    const bool wifi = is_wifi_clone() ||
        info.wifi_role == wf_net::WifiRole::ParentRadio ||
        info.kind == wf_net::InterfaceKind::Wireless;

    remove_css_class("is-up");
    remove_css_class("is-down");
    const auto wifi_st = wf_net::format_wifi_connection_state(info);
    const bool wifi_connected = (wifi_st == "Connected" || wifi_st == "Associated");
    const bool wifi_iface_on = up; /* IFF_UP — radio/stack on, not “associated” */
    if (wifi && wifi_connected)
    {
        add_css_class("is-up");
    } else if (up && !wifi)
    {
        add_css_class("is-up");
    } else if (wifi && up && !wifi_connected)
    {
        /* On but not associated — amber-ish via is-down opacity + not fully red story */
        add_css_class("is-down");
    } else
    {
        add_css_class("is-down");
    }

    /*
     * Title is the connection story:
     *   Connected → SSID (primary), iface as secondary in subtitle if needed
     *   Not connected → wlan0 · Disconnected / On
     */
    std::string title;
    if (info.wifi_role == wf_net::WifiRole::ParentRadio && info.wifi_needs_clone)
    {
        title = info.name + " · no wlan";
    } else if (wifi && wifi_connected && !info.wifi_ssid.empty())
    {
        title = info.wifi_ssid;
    } else if (wifi)
    {
        title = info.name;
        if (!wifi_st.empty() && wifi_st != "On")
        {
            title += " · ";
            title += wifi_st;
        }
    } else if (info.is_default_route)
    {
        title = info.name + " · default";
    } else
    {
        title = info.name;
    }
    name_lbl_.set_text(title);
    icon_.set_from_icon_name(net_->get_icon_symbolic());

    auto speed = wf_net::format_iface_speed(info);
    speed_lbl_.set_text(speed);
    speed_lbl_.set_visible(!speed.empty());

    /* Subtitle: Connected · addresses  (no scan chrome / no wpa: noise) */
    std::string sub;
    if (wifi && is_wifi_clone())
    {
        if (wifi_connected)
        {
            sub = "Connected";
            if (info.is_default_route)
            {
                sub += " · default route";
            }
            if (info.wifi_signal_pct > 0)
            {
                sub += " · ";
                sub += std::to_string(static_cast<unsigned>(info.wifi_signal_pct));
                sub += "%";
            } else if (info.wifi_signal_dbm != 0)
            {
                sub += " · ";
                sub += std::to_string(info.wifi_signal_dbm);
                sub += " dBm";
            }
            if (!info.name.empty())
            {
                sub += " · ";
                sub += info.name;
            }
            auto addrs = wf_net::format_address_summary(info, 4);
            if (!addrs.empty())
            {
                sub += "\n";
                sub += addrs;
            }
        } else
        {
            auto addrs = wf_net::format_address_summary(info, 4);
            if (!addrs.empty())
            {
                sub = addrs;
            }
        }
    } else if (parent_only)
    {
        sub = "Turn on to enable Wi‑Fi";
    } else
    {
        sub = wf_net::format_address_summary(info, 4);
        if (sub.empty() && !info.media.empty() && speed.empty())
        {
            sub = info.media;
        }
    }
    sub_lbl_.set_text(sub);
    sub_lbl_.set_visible(!sub.empty());

    btn_toggle_.set_label(wifi_iface_on ? "Turn off" : "Turn on");
    const bool can_admin = (wf_net::probe_admin_privilege() != wf_net::AdminPrivilege::None);
    btn_toggle_.set_sensitive(can_admin && !power_busy_);
    btn_delete_.set_visible(can_admin && wf_net::is_destroyable_iface(info.name));

    /* Expand chrome + revealer track expanded_ (user-controlled per iface). */
    if (!is_wifi_clone() || !up)
    {
        expanded_ = false;
    }
    update_expand_chrome();
    ap_revealer_.set_reveal_child(expanded_ && popover_open_ && is_wifi_clone() && up);

    if (wifi)
    {
        static thread_local std::string last_ui_fp;
        std::string ui_fp = info.name + "|" + wifi_st + "|" + info.wifi_ssid + "|" +
            info.wifi_wpa_state + "|" + (up ? "1" : "0") + "|" +
            (scanning_ ? "1" : "0") + "|" + (popover_open_ ? "1" : "0") + "|" +
            (expanded_ ? "1" : "0");
        if (ui_fp != last_ui_fp)
        {
            last_ui_fp = ui_fp;
            auto fields = wf_net::wifi_state_fields(info);
            fields.push_back(wf_net::field_str("ui_title", title));
            fields.push_back(wf_net::field_str("ui_toggle", btn_toggle_.get_label()));
            fields.push_back(wf_net::field_bool("ui_scanning", scanning_));
            fields.push_back(wf_net::field_bool("popover_open", popover_open_));
            fields.push_back(wf_net::field_bool("expanded", expanded_));
            wf_net::net_event_info("wifi.ui.refresh", fields, info.name);
        }
    }
}

void FreeBSDIfaceRow::show_context_menu(double x, double y)
{
    refresh();
    const bool can_admin = (wf_net::probe_admin_privilege() != wf_net::AdminPrivilege::None);
    const bool can_del = can_admin && net_ &&
        wf_net::is_destroyable_iface(net_->get_interface());
    btn_toggle_.set_visible(can_admin);
    btn_delete_.set_visible(can_del);

    Gdk::Rectangle r;
    r.set_x(static_cast<int>(x));
    r.set_y(static_cast<int>(y));
    r.set_width(1);
    r.set_height(1);
    menu_.set_pointing_to(r);
    menu_.popup();
}

void FreeBSDIfaceRow::elevate_ifconfig(const char *verb)
{
    if (!net_ || !verb)
    {
        return;
    }
    const std::string name = net_->get_interface();
    if (std::strcmp(verb, "up") != 0 && std::strcmp(verb, "down") != 0 &&
        std::strcmp(verb, "destroy") != 0)
    {
        return;
    }
    if (net_->info().wifi_role == wf_net::WifiRole::ParentRadio)
    {
        return;
    }
    if (std::strcmp(verb, "destroy") == 0 && !wf_net::is_destroyable_iface(name))
    {
        return;
    }
    run_elevated("ifconfig " + name + " " + verb);
}

void FreeBSDIfaceRow::do_toggle()
{
    if (!net_ || power_busy_)
    {
        return;
    }
    const auto info = net_->info();
    const bool wifi = info.kind == wf_net::InterfaceKind::Wireless ||
        info.wifi_role != wf_net::WifiRole::None;

    if (wifi)
    {
        power_busy_ = true;
        btn_toggle_.set_sensitive(false);
        const std::string name = info.name;
        const bool turn_off = info.up && is_wifi_clone();
        auto alive = alive_;
        std::thread([this, alive, name, turn_off] () {
            wf_net::WifiPowerResult r = turn_off ?
                wf_net::wifi_turn_off(name) : wf_net::wifi_turn_on(name);
            ui_idle([this, alive, r, turn_off] () {
                if (!alive->load())
                {
                    return;
                }
                power_busy_ = false;
                if (r.ok && net_)
                {
                    auto i = net_->info();
                    if (turn_off)
                    {
                        i.up = false;
                        i.running = false;
                        i.wifi_ssid.clear();
                    } else
                    {
                        i.up = true;
                    }
                    net_->update_info(i);
                }
                refresh();
                if (r.ok && !turn_off && popover_open_)
                {
                    do_scan();
                }
            });
        }).detach();
        return;
    }

    /* Ethernet / other: plain ifconfig up/down (fast). */
    elevate_ifconfig(info.up ? "down" : "up");
    auto i = info;
    i.up = !i.up;
    if (!i.up)
    {
        i.running = false;
    }
    net_->update_info(i);
    refresh();
}

void FreeBSDIfaceRow::do_delete()
{
    if (!net_ || !wf_net::is_destroyable_iface(net_->get_interface()))
    {
        return;
    }
    details_.reset();
    if (collector_)
    {
        collector_->unwatch(net_->get_interface());
    }
    elevate_ifconfig("destroy");
}

void FreeBSDIfaceRow::do_create_wlan()
{
    /* Same as Turn on — never block the UI thread. */
    do_toggle();
}

void FreeBSDIfaceRow::rebuild_ap_list(const std::vector<wf_net::WifiScanEntry>& aps)
{
    while (auto *ch = ap_box_.get_first_child())
    {
        ap_box_.remove(*ch);
    }

    /* Per-interface: only this wlan's association is "connected". */
    const std::string wlan = wlan_name();
    const std::string cur_ssid = net_ ? net_->info().wifi_ssid : std::string{};
    const std::string cur_bssid = net_ ? net_->info().wifi_bssid : std::string{};
    const bool iface_connected =
        net_ &&
        (net_->info().wifi_wpa_state == "COMPLETED" ||
         net_->info().status == "associated") &&
        !cur_ssid.empty();

    std::vector<wf_net::WifiScanEntry> sorted = aps;
    /* Ensure current SSID appears even if scan missed it (per this iface). */
    if (iface_connected)
    {
        bool found = false;
        for (const auto& ap : sorted)
        {
            if (ap.ssid == cur_ssid)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            wf_net::WifiScanEntry cur;
            cur.ssid = cur_ssid;
            cur.bssid = cur_bssid;
            cur.freq_mhz = 0;
            cur.signal_dbm = net_->info().wifi_signal_dbm != 0 ?
                net_->info().wifi_signal_dbm : -50;
            cur.security = "wpa";
            sorted.insert(sorted.begin(), std::move(cur));
        }
    }

    /* Connected network first, then by signal (scan list already sorted). */
    std::stable_sort(sorted.begin(), sorted.end(),
        [&] (const wf_net::WifiScanEntry& a, const wf_net::WifiScanEntry& b) {
            const bool ac = iface_connected && a.ssid == cur_ssid;
            const bool bc = iface_connected && b.ssid == cur_ssid;
            if (ac != bc)
            {
                return ac && !bc;
            }
            return a.signal_dbm > b.signal_dbm;
        });

    /* SSIDs already in wpa_supplicant — no password prompt on click. */
    std::vector<std::string> saved = wf_net::wifi_saved_ssids(wlan);
    auto is_saved_ssid = [&] (const std::string& s) {
        for (const auto& x : saved)
        {
            if (x == s)
            {
                return true;
            }
        }
        return false;
    };

    int shown = 0;
    for (const auto& ap : sorted)
    {
        const bool is_cur = iface_connected && ap.ssid == cur_ssid;
        const bool saved_ap = is_saved_ssid(ap.ssid);
        auto *row = Gtk::make_managed<FreeBSDApRow>(wlan, ap, is_cur, saved_ap);
        if (!is_cur)
        {
            row->signal_activated().connect(
                [this] (const wf_net::WifiScanEntry& e) { do_join(e); });
        }
        if (saved_ap || is_cur)
        {
            row->signal_connect_req().connect(
                [this] (const wf_net::WifiScanEntry& e) { do_join(e); });
            row->signal_disconnect_req().connect(
                [this] (const wf_net::WifiScanEntry& e) { do_disconnect_ap(e); });
            row->signal_change_password_req().connect(
                [this] (const wf_net::WifiScanEntry& e) { do_change_password_ap(e); });
            row->signal_forget_req().connect(
                [this] (const wf_net::WifiScanEntry& e) { do_forget_ap(e); });
        }
        ap_box_.append(*row);
        ++shown;
    }
    ap_revealer_.set_reveal_child(expanded_ && popover_open_ && is_wifi_clone() &&
        net_ && net_->info().up && shown > 0);
    wf_net::net_event_info("wifi.scan.ui", {
        wf_net::field_int("shown", shown),
        wf_net::field_str("wlan", wlan),
        wf_net::field_str("current_ssid", cur_ssid),
        wf_net::field_bool("iface_connected", iface_connected),
        wf_net::field_bool("expanded", expanded_),
    }, wlan);
}

void FreeBSDIfaceRow::do_scan()
{
    /* Only scan when this iface list is expanded and popover is open. */
    if (!is_wifi_clone() || scanning_ || !popover_open_ || !expanded_)
    {
        return;
    }
    if (!net_ || !net_->info().up)
    {
        return;
    }
    scanning_ = true;

    const std::string wlan = wlan_name();
    auto alive = alive_;
    std::thread([this, alive, wlan] () {
        /* ≥4s: associated iface BSS cache fills slowly with neighbors. */
        auto aps = wf_net::wifi_scan(wlan, 4000);
        ui_idle([this, alive, aps = std::move(aps)] () mutable {
            if (!alive->load())
            {
                return;
            }
            scanning_ = false;
            if (popover_open_ && expanded_)
            {
                rebuild_ap_list(aps);
            }
            refresh();
        });
    }).detach();
}

void FreeBSDIfaceRow::do_join(const wf_net::WifiScanEntry& ap)
{
    if (!is_wifi_clone() || power_busy_)
    {
        return;
    }
    const std::string wlan = wlan_name();
    const std::string ssid = ap.ssid;
    const std::string security = ap.security;

    auto apply_join_result = [this] (const std::string& joined_ssid,
        const wf_net::WifiPowerResult& r) {
        if (r.ok && net_)
        {
            auto i = net_->info();
            i.wifi_ssid = joined_ssid;
            i.up = true;
            i.running = true;
            i.wifi_wpa_state = "COMPLETED";
            i.status = "associated";
            net_->update_info(i);
        }
        refresh();
        if (popover_open_)
        {
            do_scan();
        }
    };

    /*
     * Saved network in wpa_supplicant: connect with stored PSK — never
     * re-prompt for the password.
     */
    if (security != "open" && wf_net::wifi_ssid_is_saved(wlan, ssid))
    {
        auto row_alive = alive_;
        std::thread([row_alive, wlan, ssid, security, apply_join_result] () {
            auto r = wf_net::wifi_join(wlan, ssid, security, ""); /* empty = use saved */
            ui_idle([row_alive, ssid, r, apply_join_result] () {
                if (!row_alive->load())
                {
                    return;
                }
                apply_join_result(ssid, r);
            });
        }).detach();
        return;
    }

    if (security != "open")
    {
        /*
         * Password dialog: worker thread must NOT hold raw Gtk* pointers.
         * Only plain strings cross the thread boundary; UI updates only on
         * the main loop, and dialog lifetime is gated by a shared flag.
         */
        auto *dlg = new Gtk::Window();
        auto dlg_alive = std::make_shared<std::atomic<bool>>(true);
        dlg->set_title("Join " + ssid);
        dlg->set_default_size(400, 220);
        dlg->set_modal(true);
        dlg->set_deletable(true);
        dlg->set_hide_on_close(true);
        /*
         * Do not set_transient_for the panel (layer-shell): dialogs parented
         * to it often fail to close under Wayland. Modal still blocks input.
         */
        dlg->unset_transient_for();
        auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        box->set_margin(18);
        auto *lbl = Gtk::make_managed<Gtk::Label>("Password for “" + ssid + "”");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_margin_bottom(2);
        /*
         * Use Entry (not PasswordEntry): peek-icon PasswordEntry clips
         * descenders/top of glyphs when visibility is toggled on FreeBSD/Adwaita.
         */
        auto *entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_visibility(false);
        entry->set_input_purpose(Gtk::InputPurpose::PASSWORD);
        entry->set_hexpand(true);
        entry->set_size_request(-1, 40);
        entry->add_css_class("wifi-password-entry");
        entry->set_margin_top(4);
        entry->set_margin_bottom(4);
        auto *show = Gtk::make_managed<Gtk::CheckButton>("Show password");
        show->signal_toggled().connect([entry, show] () {
            entry->set_visibility(show->get_active());
        });
        auto *err = Gtk::make_managed<Gtk::Label>("");
        err->add_css_class("error");
        err->set_halign(Gtk::Align::START);
        err->set_margin_top(2);
        auto *btns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btns->set_halign(Gtk::Align::END);
        btns->set_margin_top(6);
        auto *cancel = Gtk::make_managed<Gtk::Button>("Cancel");
        auto *join = Gtk::make_managed<Gtk::Button>("Join");
        join->add_css_class("suggested-action");
        btns->append(*cancel);
        btns->append(*join);
        box->append(*lbl);
        box->append(*entry);
        box->append(*show);
        box->append(*err);
        box->append(*btns);
        dlg->set_child(*box);

        auto finish = [dlg, dlg_alive] () {
            if (!dlg_alive->exchange(false))
            {
                return; /* already finished */
            }
            dlg->hide();
            /* Delete next idle tick so signal handlers unwind first. */
            g_idle_add(
                [] (gpointer p) -> gboolean {
                    delete static_cast<Gtk::Window*>(p);
                    return G_SOURCE_REMOVE;
                },
                dlg);
        };
        cancel->signal_clicked().connect(finish);
        dlg->signal_close_request().connect([finish] () {
            finish();
            return true; /* we handled close */
        }, false);

        join->signal_clicked().connect(
            [this, entry, err, join, ssid, security, wlan, finish, dlg_alive,
                apply_join_result] () {
            if (!dlg_alive->load())
            {
                return;
            }
            std::string k = std::string(entry->get_text());
            auto v = wf_net::validate_wifi_credentials(security, k);
            if (!v.ok)
            {
                err->set_text(v.message);
                return;
            }
            err->set_text("Connecting…");
            join->set_sensitive(false);
            auto row_alive = alive_;
            /* Capture only POD/strings for the worker — no Gtk widgets. */
            std::thread([row_alive, dlg_alive, finish, err, join,
                ssid, security, wlan, k, apply_join_result] () {
                auto r = wf_net::wifi_join(wlan, ssid, security, k);
                ui_idle([row_alive, dlg_alive, finish, err, join,
                    ssid, r, apply_join_result] () {
                    if (!dlg_alive->load())
                    {
                        return;
                    }
                    if (!r.ok)
                    {
                        err->set_text(r.detail.empty() ? "Join failed" : r.detail);
                        join->set_sensitive(true);
                        return;
                    }
                    if (row_alive->load())
                    {
                        apply_join_result(ssid, r);
                    }
                    finish();
                });
            }).detach();
        });
        dlg->present();
        entry->grab_focus();
        return;
    }

    /* Open network — strings only on the worker */
    auto row_alive = alive_;
    std::thread([row_alive, wlan, ssid, apply_join_result] () {
        auto r = wf_net::wifi_join(wlan, ssid, "open", "");
        ui_idle([row_alive, ssid, r, apply_join_result] () {
            if (!row_alive->load())
            {
                return;
            }
            apply_join_result(ssid, r);
        });
    }).detach();
}

void FreeBSDIfaceRow::do_disconnect_ap(const wf_net::WifiScanEntry& ap)
{
    (void)ap;
    if (!is_wifi_clone() || power_busy_)
    {
        return;
    }
    const std::string wlan = wlan_name();
    auto row_alive = alive_;
    std::thread([this, row_alive, wlan] () {
        auto r = wf_net::wifi_disconnect(wlan);
        ui_idle([this, row_alive, r] () {
            if (!row_alive->load() || !net_)
            {
                return;
            }
            if (r.ok)
            {
                auto i = net_->info();
                i.wifi_ssid.clear();
                i.wifi_bssid.clear();
                i.wifi_wpa_state = "DISCONNECTED";
                i.status = "no carrier";
                i.running = false;
                net_->update_info(i);
            }
            refresh();
            if (popover_open_)
            {
                do_scan();
            }
        });
    }).detach();
}

void FreeBSDIfaceRow::do_forget_ap(const wf_net::WifiScanEntry& ap)
{
    if (!is_wifi_clone() || power_busy_)
    {
        return;
    }
    const std::string wlan = wlan_name();
    const std::string ssid = ap.ssid;
    auto row_alive = alive_;
    std::thread([this, row_alive, wlan, ssid] () {
        auto r = wf_net::wifi_forget_network(wlan, ssid);
        ui_idle([this, row_alive, ssid, r] () {
            if (!row_alive->load())
            {
                return;
            }
            if (r.ok && net_ && net_->info().wifi_ssid == ssid)
            {
                auto i = net_->info();
                i.wifi_ssid.clear();
                i.wifi_bssid.clear();
                i.wifi_wpa_state = "DISCONNECTED";
                i.status = "no carrier";
                i.running = false;
                net_->update_info(i);
            }
            refresh();
            if (popover_open_)
            {
                do_scan();
            }
        });
    }).detach();
}

void FreeBSDIfaceRow::do_change_password_ap(const wf_net::WifiScanEntry& ap)
{
    if (!is_wifi_clone() || power_busy_)
    {
        return;
    }
    const std::string wlan = wlan_name();
    const std::string ssid = ap.ssid;
    const std::string security = ap.security.empty() ? "wpa" : ap.security;

    auto *dlg = new Gtk::Window();
    auto dlg_alive = std::make_shared<std::atomic<bool>>(true);
    dlg->set_title("Change password — " + ssid);
    dlg->set_default_size(400, 220);
    dlg->set_modal(true);
    dlg->set_deletable(true);
    dlg->set_hide_on_close(true);
    dlg->unset_transient_for();
    auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    box->set_margin(18);
    auto *lbl = Gtk::make_managed<Gtk::Label>("New password for “" + ssid + "”");
    lbl->set_halign(Gtk::Align::START);
    auto *entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_visibility(false);
    entry->set_input_purpose(Gtk::InputPurpose::PASSWORD);
    entry->set_hexpand(true);
    entry->set_size_request(-1, 40);
    entry->add_css_class("wifi-password-entry");
    auto *show = Gtk::make_managed<Gtk::CheckButton>("Show password");
    show->signal_toggled().connect([entry, show] () {
        entry->set_visibility(show->get_active());
    });
    auto *err = Gtk::make_managed<Gtk::Label>("");
    err->add_css_class("error");
    err->set_halign(Gtk::Align::START);
    auto *btns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btns->set_halign(Gtk::Align::END);
    auto *cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    auto *save = Gtk::make_managed<Gtk::Button>("Save");
    save->add_css_class("suggested-action");
    btns->append(*cancel);
    btns->append(*save);
    box->append(*lbl);
    box->append(*entry);
    box->append(*show);
    box->append(*err);
    box->append(*btns);
    dlg->set_child(*box);

    auto finish = [dlg, dlg_alive] () {
        if (!dlg_alive->exchange(false))
        {
            return;
        }
        dlg->hide();
        g_idle_add(
            [] (gpointer p) -> gboolean {
                delete static_cast<Gtk::Window*>(p);
                return G_SOURCE_REMOVE;
            },
            dlg);
    };
    cancel->signal_clicked().connect(finish);
    dlg->signal_close_request().connect([finish] () {
        finish();
        return true;
    }, false);

    save->signal_clicked().connect(
        [this, entry, err, save, ssid, security, wlan, finish, dlg_alive] () {
        if (!dlg_alive->load())
        {
            return;
        }
        std::string k = std::string(entry->get_text());
        auto v = wf_net::validate_wifi_credentials(security, k);
        if (!v.ok)
        {
            err->set_text(v.message);
            return;
        }
        err->set_text("Saving…");
        save->set_sensitive(false);
        auto row_alive = alive_;
        std::thread([this, row_alive, dlg_alive, finish, err, save,
            ssid, security, wlan, k] () {
            auto r = wf_net::wifi_change_password(wlan, ssid, security, k);
            ui_idle([this, row_alive, dlg_alive, finish, err, save, r] () {
                if (!dlg_alive->load())
                {
                    return;
                }
                if (!r.ok)
                {
                    err->set_text(r.detail.empty() ? "Failed" : r.detail);
                    save->set_sensitive(true);
                    return;
                }
                if (row_alive->load())
                {
                    refresh();
                    if (popover_open_)
                    {
                        do_scan();
                    }
                }
                finish();
            });
        }).detach();
    });
    dlg->present();
    entry->grab_focus();
}

void FreeBSDIfaceRow::do_details()
{
    if (!net_)
    {
        return;
    }

    /*
     * Close the network tray popover first. Details closes cleanly alone;
     * keeping the popover open under it fights focus/grabs on Wayland.
     */
    for (Gtk::Widget *w = this; w; w = w->get_parent())
    {
        if (auto *pop = dynamic_cast<Gtk::Popover*>(w))
        {
            pop->popdown();
            break;
        }
    }
    /* Also collapse local context menu if still up. */
    menu_.popdown();

    /* Already open — just raise it. */
    if (details_ && details_->get_visible())
    {
        details_->present_for(nullptr);
        return;
    }
    /* Fresh window each open avoids stuck hide/map state on Wayland. */
    details_.reset();
    details_ = std::make_unique<FreeBSDDetailsWindow>(net_, collector_);
    signals_.push_back(details_->signal_closed().connect([this] () {
        /*
         * closed_ is already emitted from an idle after hide — safe to
         * destroy the C++ object immediately.
         */
        details_.reset();
    }));
    /*
     * Pass the panel window only as a display source (not transient-for).
     * Layer-shell as transient parent is what made Close/Escape feel broken.
     */
    Gtk::Window *panel_win = nullptr;
    if (auto *root = get_root())
    {
        panel_win = dynamic_cast<Gtk::Window*>(root);
    }
    details_->present_for(panel_win);
}

/* ─── FreeBSDDetailsWindow ───────────────────────────────────────────────── */

FreeBSDDetailsWindow::FreeBSDDetailsWindow(std::shared_ptr<FreeBSDNetwork> net,
    wf_net::TrafficCollector *collector) :
    net_(std::move(net)),
    collector_(collector)
{
    set_title(net_ ? ("Network — " + net_->get_interface()) : "Interface");
    set_default_size(480, 540);
    set_modal(false);
    set_destroy_with_parent(false);
    set_hide_on_close(true);
    set_deletable(true);
    set_resizable(true);
    /* Standalone toplevel — never transient-for layer-shell panel. */
    unset_transient_for();
    attach_to_app();

    root_.set_margin(12);
    root_.set_spacing(10);
    set_child(root_);

    props_.set_column_spacing(10);
    props_.set_row_spacing(6);
    props_.set_hexpand(true);
    root_.append(props_);

    auto *traffic_head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto *traffic_lbl  = Gtk::make_managed<Gtk::Label>("Traffic");
    traffic_lbl->set_halign(Gtk::Align::START);
    traffic_lbl->set_hexpand(true);
    traffic_lbl->add_css_class("title-4");
    for (int i = 0; wf_net::TRAFFIC_GRAPH_STYLES[i]; ++i)
    {
        const char *s = wf_net::TRAFFIC_GRAPH_STYLES[i];
        graph_combo_.append(s, s);
    }
    graph_style_ = load_persisted_graph_style();
    graph_combo_.set_active_id(graph_style_);
    traffic_head->append(*traffic_lbl);
    traffic_head->append(graph_combo_);
    root_.append(*traffic_head);

    traffic_meta_.set_halign(Gtk::Align::START);
    traffic_meta_.set_wrap(true);
    traffic_meta_.add_css_class("dim-label");
    root_.append(traffic_meta_);

    graph_.set_content_width(420);
    graph_.set_content_height(140);
    graph_.set_hexpand(true);
    graph_.set_vexpand(false);
    /* Lambda (not mem_fun) — matches volume meters; avoids dangling slot edge cases. */
    graph_.set_draw_func([this] (const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        draw_graph(cr, w, h);
    });
    root_.append(graph_);

    close_btn_.set_label("Close");
    close_btn_.set_halign(Gtk::Align::END);
    close_btn_.set_can_focus(true);
    close_btn_.set_receives_default(true);
    set_default_widget(close_btn_);
    signals_.push_back(close_btn_.signal_clicked().connect(
        [this] () { request_close(); }));
    root_.append(close_btn_);

    signals_.push_back(graph_combo_.signal_changed().connect(
        [this] () { on_graph_style_changed(); }));

    /* Title-bar X / Alt-F4 → close-request. */
    signals_.push_back(signal_close_request().connect([this] () {
        request_close();
        return true; /* we handle hide + deferred destroy */
    }, false));

    /* Explicit Escape — some Wayland compositors don't map it to close-request. */
    auto key = Gtk::EventControllerKey::create();
    signals_.push_back(key->signal_key_pressed().connect(
        [this] (guint keyval, guint, Gdk::ModifierType) -> bool {
            if (keyval == GDK_KEY_Escape)
            {
                request_close();
                return true;
            }
            return false;
        }, false));
    add_controller(key);

    if (net_ && collector_ &&
        wf_net::is_valid_traffic_ifname(net_->get_interface()) &&
        net_->info().wifi_role != wf_net::WifiRole::ParentRadio)
    {
        collector_->watch(net_->get_interface());
    }

    rebuild_props();
    update_traffic_meta();
}

FreeBSDDetailsWindow::~FreeBSDDetailsWindow()
{
    stop_tick();
    if (close_idle_.connected())
    {
        close_idle_.disconnect();
    }
    for (auto& s : signals_)
    {
        s.disconnect();
    }
    try
    {
        unset_application();
    } catch (...)
    {
    }
}

void FreeBSDDetailsWindow::attach_to_app()
{
    /*
     * Wayland: windows not tied to the GtkApplication often mishandle close.
     * Gtk::Application::get_default() returns Gtk::Application (not Gio).
     */
    auto app = std::dynamic_pointer_cast<Gtk::Application>(
        Gio::Application::get_default());
    if (app)
    {
        set_application(app);
    }
}

void FreeBSDDetailsWindow::stop_tick()
{
    if (tick_.connected())
    {
        tick_.disconnect();
    }
}

void FreeBSDDetailsWindow::ensure_tick()
{
    if (tick_.connected())
    {
        return;
    }
    tick_ = Glib::signal_timeout().connect(
        [this] () { return on_tick(); }, 1000);
}

void FreeBSDDetailsWindow::request_close()
{
    if (closing_)
    {
        return;
    }
    closing_ = true;
    stop_tick();
    /* Unmap first while the window object is still fully alive. */
    if (get_visible())
    {
        hide();
    }
    /*
     * Emit closed_ only after the current GTK signal stack unwinds.
     * Destroying (unique_ptr reset) from inside close-request/clicked is
     * what left the modal half-dead / unresponsive on Wayland.
     */
    if (close_idle_.connected())
    {
        close_idle_.disconnect();
    }
    close_idle_ = Glib::signal_idle().connect([this] () {
        closed_.emit();
        return false; /* one-shot */
    });
}

void FreeBSDDetailsWindow::present_for(Gtk::Window *display_source)
{
    closing_ = false;
    if (close_idle_.connected())
    {
        close_idle_.disconnect();
    }
    /*
     * Never set_transient_for the panel (layer-shell). Do inherit its
     * GdkDisplay so the dialog lands on the same Wayland seat/output.
     */
    unset_transient_for();
    set_modal(false);
    set_destroy_with_parent(false);
    if (display_source)
    {
        if (auto disp = display_source->get_display())
        {
            set_display(disp);
        }
    }
    attach_to_app();
    rebuild_props();
    update_traffic_meta();
    present();
    ensure_tick();
    close_btn_.grab_focus();
}

void FreeBSDDetailsWindow::on_graph_style_changed()
{
    graph_style_ = wf_net::safe_traffic_graph_style(graph_combo_.get_active_id());
    save_persisted_graph_style(graph_style_);
    graph_.queue_draw();
}

bool FreeBSDDetailsWindow::on_tick()
{
    if (closing_ || !get_visible())
    {
        return false; /* drop timer while hidden/closing */
    }
    if (net_)
    {
        const std::string fp = wf_net::interface_fingerprint(net_->info());
        if (fp != props_fp_)
        {
            rebuild_props();
        }
    }
    update_traffic_meta();
    graph_.queue_draw();
    return true;
}

void FreeBSDDetailsWindow::update_traffic_meta()
{
    if (!collector_ || !net_)
    {
        traffic_meta_.set_text("");
        return;
    }
    auto s = collector_->snapshot(net_->get_interface());
    traffic_meta_.set_text(
        "RX " + wf_net::format_bit_rate_from_bytes(s.last_rx_Bps) +
        " · TX " + wf_net::format_bit_rate_from_bytes(s.last_tx_Bps) +
        "\nTotal RX " + wf_net::format_byte_count(s.rx_total) +
        " · TX " + wf_net::format_byte_count(s.tx_total) +
        "\nHistory " + std::to_string(s.samples.size()) + "s / " +
        std::to_string(wf_net::k_traffic_history_sec) + "s");
}

void FreeBSDDetailsWindow::rebuild_props()
{
    /* Clear grid without thrashing layout mid-measure if possible. */
    while (auto *ch = props_.get_first_child())
    {
        props_.remove(*ch);
    }
    if (!net_)
    {
        props_fp_.clear();
        return;
    }
    const auto& info = net_->info();
    props_fp_ = wf_net::interface_fingerprint(info);

    int row = 0;
    auto add_row = [&] (const char *k, const std::string& v) {
        auto *lk = Gtk::make_managed<Gtk::Label>(k);
        auto *lv = Gtk::make_managed<Gtk::Label>(v.empty() ? "—" : v);
        lk->set_halign(Gtk::Align::START);
        lk->add_css_class("dim-label");
        lv->set_halign(Gtk::Align::START);
        lv->set_hexpand(true);
        lv->set_wrap(true);
        lv->set_selectable(true);
        props_.attach(*lk, 0, row, 1, 1);
        props_.attach(*lv, 1, row, 1, 1);
        ++row;
    };

    add_row("Name", info.name);
    add_row("Kind", wf_net::kind_label(info.kind));
    if (info.wifi_role == wf_net::WifiRole::ParentRadio)
    {
        add_row("Wi-Fi role", info.wifi_needs_clone ? "Parent radio (no wlan)" : "Parent radio");
    } else if (info.wifi_role == wf_net::WifiRole::WlanClone)
    {
        add_row("Wi-Fi role", "wlan clone");
        add_row("Parent radio", info.wifi_parent);
        add_row("SSID", info.wifi_ssid);
        if (info.wifi_channel > 0)
        {
            add_row("Channel", std::to_string(info.wifi_channel));
        }
    }
    if (!info.groups.empty())
    {
        std::string gs;
        for (const auto& g : info.groups)
        {
            if (!gs.empty())
            {
                gs += " ";
            }
            gs += g;
        }
        add_row("Groups", gs);
    }
    if (info.kind == wf_net::InterfaceKind::Wireless ||
        info.wifi_role != wf_net::WifiRole::None)
    {
        add_row("Wi‑Fi state", wf_net::format_wifi_connection_state(info));
        if (!info.wifi_wpa_state.empty())
        {
            add_row("wpa_state", info.wifi_wpa_state);
        }
        if (!info.wifi_bssid.empty())
        {
            add_row("BSSID", info.wifi_bssid);
        }
    }
    add_row("State", info.up ? (info.running ? "Up · running" : "Up · no link") : "Down");
    add_row("Speed", wf_net::format_iface_speed(info));
    add_row("Media", info.media);
    add_row("Status", info.status);
    add_row("MAC", info.mac);
    {
        std::string v4;
        for (const auto& a : info.ipv4)
        {
            if (!v4.empty())
            {
                v4 += "\n";
            }
            v4 += a;
        }
        add_row("IPv4", v4);
    }
    {
        std::string v6;
        for (const auto& a : info.ipv6)
        {
            if (!v6.empty())
            {
                v6 += "\n";
            }
            v6 += a;
        }
        add_row("IPv6", v6.empty() ? "—" : v6);
    }
    add_row("Gateway v4", info.gateway_v4);
    add_row("Gateway v6", info.gateway_v6);
    add_row("Default route", info.is_default_route ? "yes" : "no");
}

void FreeBSDDetailsWindow::draw_graph(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // Draw a premium dark purple gradient background for the scope
    Cairo::RefPtr<Cairo::LinearGradient> bg_grad = Cairo::LinearGradient::create(0.0, h, w, 0.0);
    bg_grad->add_color_stop_rgb(0.0, 0.04, 0.03, 0.08);
    bg_grad->add_color_stop_rgb(1.0, 0.06, 0.05, 0.12);
    cr->set_source(bg_grad);
    cr->rectangle(0, 0, w, h);
    cr->fill();

    // Draw a subtle Miami Cyberpunk grid in the background
    cr->set_source_rgba(1.0, 0.0, 0.5, 0.04); // Faint hot pink
    cr->set_line_width(0.6);
    for (int x = 0; x < w; x += 16) {
        cr->move_to(x, 0);
        cr->line_to(x, h);
        cr->stroke();
    }
    for (int y = 0; y < h; y += 12) {
        cr->move_to(0, y);
        cr->line_to(w, y);
        cr->stroke();
    }

    if (!collector_ || !net_ ||
        net_->info().wifi_role == wf_net::WifiRole::ParentRadio)
    {
        cr->set_source_rgb(0.4, 0.4, 0.45);
        cr->move_to(12, h / 2.0);
        cr->show_text("No traffic for this interface");
        return;
    }

    auto series = collector_->snapshot(net_->get_interface());
    if (series.samples.empty())
    {
        cr->set_source_rgb(0.4, 0.4, 0.45);
        cr->move_to(12, h / 2.0);
        cr->show_text("Collecting traffic…");
        return;
    }

    const double pad_t = 16, pad_b = 10, pad_l = 8, pad_r = 8;
    const double plot_w = std::max(1.0, w - pad_l - pad_r);
    const double plot_h = std::max(1.0, h - pad_t - pad_b);
    const size_t n = series.samples.size();
    double maxv = 1.0;
    for (const auto& s : series.samples)
    {
        maxv = std::max(maxv, static_cast<double>(s.rx_Bps));
        maxv = std::max(maxv, static_cast<double>(s.tx_Bps));
    }

    auto x_at = [&] (size_t i) {
        return pad_l + (n <= 1 ? plot_w / 2.0 : (static_cast<double>(i) / (n - 1)) * plot_w);
    };
    auto y_of = [&] (double v) {
        return pad_t + plot_h - (v / maxv) * plot_h;
    };

    const std::string style = wf_net::safe_traffic_graph_style(graph_style_);

    auto stroke_line = [&] (bool rx_side, double line_w = 1.5) {
        cr->begin_new_path();
        for (size_t i = 0; i < n; ++i)
        {
            double v = rx_side ? series.samples[i].rx_Bps : series.samples[i].tx_Bps;
            double x = x_at(i), y = y_of(v);
            if (i == 0)
            {
                cr->move_to(x, y);
            } else
            {
                cr->line_to(x, y);
            }
        }
        
        double r = rx_side ? 0.0 : 1.0;
        double g = rx_side ? 0.94 : 0.0;
        double b = rx_side ? 1.0 : 0.5;

        // Draw multi-pass neon glow for the line
        // Pass 1: Bloom
        cr->set_source_rgba(r, g, b, 0.18);
        cr->set_line_width(line_w * 3.0);
        cr->stroke_preserve();

        // Pass 2: Light glow
        cr->set_source_rgba(r, g, b, 0.45);
        cr->set_line_width(line_w * 1.8);
        cr->stroke_preserve();

        // Pass 3: Core beam
        cr->set_source_rgba(r, g, b, 1.0);
        cr->set_line_width(line_w);
        cr->stroke();
    };

    auto fill_under = [&] (bool rx_side) {
        cr->begin_new_path();
        for (size_t i = 0; i < n; ++i)
        {
            double v = rx_side ? series.samples[i].rx_Bps : series.samples[i].tx_Bps;
            double x = x_at(i), y = y_of(v);
            if (i == 0)
            {
                cr->move_to(x, y);
            } else
            {
                cr->line_to(x, y);
            }
        }
        cr->line_to(x_at(n - 1), pad_t + plot_h);
        cr->line_to(x_at(0), pad_t + plot_h);
        cr->close_path();

        double r = rx_side ? 0.0 : 1.0;
        double g = rx_side ? 0.94 : 0.0;
        double b = rx_side ? 1.0 : 0.5;

        // Draw translucent gradient fill
        Cairo::RefPtr<Cairo::LinearGradient> area_grad = Cairo::LinearGradient::create(0.0, pad_t + plot_h, 0.0, pad_t);
        area_grad->add_color_stop_rgba(0.0, r, g, b, 0.02);
        area_grad->add_color_stop_rgba(1.0, r, g, b, 0.20);
        cr->set_source(area_grad);
        cr->fill();

        // Stroke glowing edge
        stroke_line(rx_side);
    };

    if (style == "wave")
    {
        stroke_line(true);
        stroke_line(false);
    } else if (style == "mirror")
    {
        const double mid = pad_t + plot_h / 2.0;

        // Symmetrical filled area
        cr->begin_new_path();
        for (size_t i = 0; i < n; ++i)
        {
            double y = mid - (series.samples[i].rx_Bps / maxv) * (plot_h / 2.0);
            if (i == 0)
            {
                cr->move_to(x_at(i), y);
            } else
            {
                cr->line_to(x_at(i), y);
            }
        }
        for (size_t i = n; i-- > 0; )
        {
            double y = mid + (series.samples[i].tx_Bps / maxv) * (plot_h / 2.0);
            cr->line_to(x_at(i), y);
        }
        cr->close_path();

        // Translucent magenta/cyan blend
        Cairo::RefPtr<Cairo::LinearGradient> fill_grad = Cairo::LinearGradient::create(0.0, mid - plot_h/2.0, 0.0, mid + plot_h/2.0);
        fill_grad->add_color_stop_rgba(0.0, 0.0, 0.94, 1.0, 0.18); // Cyan top (RX)
        fill_grad->add_color_stop_rgba(0.5, 0.6, 0.0, 0.8, 0.1);  // Purple mid
        fill_grad->add_color_stop_rgba(1.0, 1.0, 0.0, 0.5, 0.18); // Pink bottom (TX)
        cr->set_source(fill_grad);
        cr->fill_preserve();

        // Glowing border
        cr->set_source_rgba(0.0, 0.94, 1.0, 1.0);
        cr->set_line_width(1.2);
        cr->stroke();
    } else if (style == "scope")
    {
        const double mid = pad_t + plot_h / 2.0;
        cr->begin_new_path();
        for (size_t i = 0; i < n; ++i)
        {
            double v = (static_cast<double>(series.samples[i].rx_Bps) -
                series.samples[i].tx_Bps * 0.55) / maxv;
            v = std::max(-1.0, std::min(1.0, v));
            if (i == 0)
            {
                cr->move_to(x_at(i), mid - v * (plot_h / 2.0));
            } else
            {
                cr->line_to(x_at(i), mid - v * (plot_h / 2.0));
            }
        }
        // Glowing purple vector beam
        cr->set_source_rgba(0.80, 0.65, 0.97, 0.15);
        cr->set_line_width(4.5);
        cr->stroke_preserve();

        cr->set_source_rgba(0.80, 0.65, 0.97, 0.45);
        cr->set_line_width(2.2);
        cr->stroke_preserve();

        cr->set_source_rgba(0.80, 0.65, 0.97, 1.0);
        cr->set_line_width(1.0);
        cr->stroke();
    } else if (style == "dots")
    {
        for (size_t i = 0; i < n; ++i)
        {
            double rx_y = y_of(series.samples[i].rx_Bps);
            double tx_y = y_of(series.samples[i].tx_Bps);

            // RX Stem & Dot (Cyan)
            cr->set_source_rgba(0.0, 0.94, 1.0, 0.15);
            cr->set_line_width(1.0);
            cr->move_to(x_at(i), pad_t + plot_h);
            cr->line_to(x_at(i), rx_y);
            cr->stroke();

            cr->set_source_rgba(0.0, 0.94, 1.0, 0.40);
            cr->arc(x_at(i), rx_y, 3.5, 0, 2 * G_PI);
            cr->fill();
            cr->set_source_rgba(0.0, 0.94, 1.0, 1.0);
            cr->arc(x_at(i), rx_y, 1.5, 0, 2 * G_PI);
            cr->fill();

            // TX Stem & Dot (Pink)
            cr->set_source_rgba(1.0, 0.0, 0.5, 0.15);
            cr->set_line_width(1.0);
            cr->move_to(x_at(i), pad_t + plot_h);
            cr->line_to(x_at(i), tx_y);
            cr->stroke();

            cr->set_source_rgba(1.0, 0.0, 0.5, 0.40);
            cr->arc(x_at(i), tx_y, 3.5, 0, 2 * G_PI);
            cr->fill();
            cr->set_source_rgba(1.0, 0.0, 0.5, 1.0);
            cr->arc(x_at(i), tx_y, 1.5, 0, 2 * G_PI);
            cr->fill();
        }
    } else if (style == "ribbon")
    {
        stroke_line(true, 5.0);
        stroke_line(false, 5.0);
        stroke_line(true, 1.2);
        stroke_line(false, 1.2);
    } else
    {
        fill_under(true);
        fill_under(false);
    }

    cr->set_source_rgba(0.0, 0.94, 1.0, 0.35);
    cr->set_line_width(1);
    cr->move_to(pad_l, pad_t + plot_h);
    cr->line_to(pad_l + plot_w, pad_t + plot_h);
    cr->stroke();
}
