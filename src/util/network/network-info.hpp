#pragma once

/**
 * FreeBSD live network probe (getifaddrs + route + ifconfig media).
 * Fail-soft: returns empty / partial on errors — never throws.
 */

#include "network-types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace wf_net
{

/**
 * Overridable I/O for unit tests (same idea as audio ProcessHooks).
 */
struct InfoHooks
{
    std::function<std::string(const std::string& /*cmd*/)> run_cmd;
};

InfoHooks& info_hooks();
void reset_info_hooks();

/** Probe all interesting interfaces on this host (FreeBSD path). */
std::vector<InterfaceInfo> probe_interfaces(const ProbeOptions& opts = {});

/**
 * Parent radio names from `sysctl -n net.wlan.devices` (FreeBSD).
 * Empty off FreeBSD / on error. Fail-soft.
 */
std::vector<std::string> list_wlan_parent_devices();

/**
 * Ensure each parent radio has at least one wlan(4) clone.
 * Uses passwordless elevators only (root / doas -n / sudo -n). Fail-soft.
 * @return number of clones created (0 if none needed or no privilege).
 */
int ensure_wlan_clones(const ProbeOptions& opts = {});

/**
 * Create one wlan clone for a parent radio (elevated ifconfig).
 * @return new wlan name on success, empty on failure.
 */
std::string create_wlan_for_parent(const std::string& parent_name,
    const std::vector<std::string>& existing_wlans = {});

/**
 * One-shot Wi‑Fi enable for FreeBSD (the “Turn on” product).
 * Accepts a parent radio (iwlwifi0) or wlan clone (wlan0).
 *
 * Does, fail-soft, in order:
 *   1. ensure wlan(4) clone exists
 *   2. ifconfig wlanN up
 *   3. ensure /etc/wpa_supplicant.conf (minimal + update_config=1)
 *   4. start wpa_supplicant -B if control socket missing
 *   5. wpa_cli reassociate / enable saved networks when possible
 *   6. dhclient wlanN for DHCP (best-effort)
 *   7. **persist boot config** via sysrc (wlans_* + ifconfig_wlan* WPA SYNCDHCP)
 *      so Wi‑Fi comes up on next reboot without the panel
 */
struct WifiPowerResult
{
    bool ok = false;
    std::string wlan;   /**< usable wlanN name after enable */
    std::string detail; /**< short human status / error */
};

WifiPowerResult wifi_turn_on(const std::string& iface_or_parent);
WifiPowerResult wifi_turn_off(const std::string& iface_or_parent);

/**
 * Write FreeBSD rc.conf entries so this wlan comes up at multiuser boot.
 * Uses sysrc; merges with existing values; never clobbers aq0/igb0 statics.
 * Idempotent. Call after successful turn-on / join.
 */
bool wifi_persist_boot_config(const std::string& wlan,
    const std::string& parent_radio = {});

/** Minimal conf body for a fresh FreeBSD install (pure). */
std::string default_wpa_supplicant_conf();

/**
 * Ensure wpa_supplicant is running for wlan, request scan, return APs.
 * Fail-soft: empty vector on error (never throws).
 */
std::vector<WifiScanEntry> wifi_scan(const std::string& wlan,
    int wait_ms = 2500);

/**
 * Join SSID on wlan via wpa_cli (add_network / set / enable / save_config).
 * security: open | wpa | wep | sae. key ignored for open.
 * If key is empty and SSID already exists in wpa_supplicant, reuses that
 * network block (no password re-entry). Starts dhclient when no IPv4 yet.
 */
WifiPowerResult wifi_join(const std::string& wlan, const std::string& ssid,
    const std::string& security, const std::string& key);

/**
 * True if wpa_supplicant already has a configured network block for SSID
 * (credentials known — UI should not prompt for the key again).
 */
bool wifi_ssid_is_saved(const std::string& wlan, const std::string& ssid);

/** All SSIDs currently configured in wpa_supplicant for this wlan. */
std::vector<std::string> wifi_saved_ssids(const std::string& wlan);

/** True if wpa_cli can talk to the iface control socket. */
bool wifi_wpa_ready(const std::string& wlan);

/** Name of the interface used by the default IPv4 route (empty if none). */
std::string default_route_interface_v4();

/** Name of the interface used by the default IPv6 route (empty if none). */
std::string default_route_interface_v6();

/** @deprecated alias — IPv4 default route interface. */
inline std::string default_route_interface()
{
    return default_route_interface_v4();
}

/** Best primary interface path among a list (default route, else first running). */
std::string pick_primary_path(const std::vector<InterfaceInfo>& list);

/** Parse `route -n get default` / `route -n get -inet6 default` (pure). */
std::string parse_route_get_interface(const std::string& text);
std::string parse_route_get_gateway(const std::string& text);

/** Parse ifconfig single-iface output for media/status/ether/inet/inet6 (pure). */
void parse_ifconfig_detail(const std::string& text, InterfaceInfo& info);

/**
 * Detect whether the current user can elevate for network mutations.
 * Order: root → doas -n → sudo -n → doas/sudo present but needs password
 * → none (information-only). Fail-soft. Never throws.
 *
 * Host doas/sudo policy is system config, not part of this repo.
 * When NeedsPassword, UI presents an auth dialog (not information-only).
 */
AdminPrivilege probe_admin_privilege(std::string *method_out = nullptr);

/** Autodetect stack features including admin / information-only gate. */
NetworkStackFeatures probe_features(const ProbeOptions& opts = {});

/**
 * Live create preflight for one clone type (FreeBSD).
 * Uses ifconfig -C + kldstat + module path existence. Fail-soft.
 * Does **not** create the interface (apply path deferred).
 */
CreatePreflight probe_create_preflight(const std::string& type);

/**
 * Types from known catalog that pass or fail preflight (for Create… UI).
 * Order matches known_clone_types(). Empty vector off FreeBSD / on error.
 */
std::vector<CreatePreflight> probe_create_catalog();

} // namespace wf_net
