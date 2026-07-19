#pragma once

/**
 * Application-agnostic network domain types (TAOCP-style clear data).
 * UI and FreeBSD/Linux backends translate live OS state into these.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace wf_net
{

/** High-level interface role for icons and filtering. */
enum class InterfaceKind
{
    Ethernet,
    Wireless,
    Bridge,
    Virtual,  /**< tap, epair, vm-port, etc. */
    Loopback,
    Other,
};

/**
 * FreeBSD 802.11 roles:
 * - ParentRadio: permanent driver device (iwlwifi0, iwm0, …) from net.wlan.devices.
 *   Not a usable IP stack iface until a wlan(4) clone is created.
 * - WlanClone: ifconfig wlanN create wlandev PARENT — the real 802.11 interface.
 */
enum class WifiRole
{
    None,
    ParentRadio,
    WlanClone,
};

/** One live interface snapshot from a FreeBSD (or mock) probe. */
struct InterfaceInfo
{
    std::string name;
    InterfaceKind kind = InterfaceKind::Other;
    bool up = false;       /**< IFF_UP */
    bool running = false;  /**< IFF_RUNNING / link */
    bool has_carrier = false;
    std::string mac;
    std::vector<std::string> ipv4;
    std::vector<std::string> ipv6; /**< global/ULA first; link-local last if kept */
    std::string media;     /**< e.g. "Ethernet autoselect …" / "IEEE 802.11 …" */
    /**
     * FreeBSD ifconfig groups (lo, wlan, bridge, tap, ether, …).
     * Populated by live probe; used for classification without name tables.
     */
    std::vector<std::string> groups;
    /**
     * Optional link bitrate in Kb/s (Wi‑Fi MaxBitrate / driver).
     * 0 = unknown; prefer format_iface_speed() which uses this then media.
     */
    unsigned link_speed_kbps = 0;
    std::string status;    /**< ifconfig status: active / no carrier */
    bool is_default_route = false;    /**< IPv4 and/or IPv6 default route egress */
    bool is_default_route_v4 = false;
    bool is_default_route_v6 = false;
    std::string gateway_v4;  /**< from route get default (may be empty) */
    std::string gateway_v6;
    /** Stable path key for maps (not a real D-Bus path on FreeBSD). */
    std::string path;

    /* ─── FreeBSD Wi‑Fi granularity ─────────────────────────────────────── */
    WifiRole wifi_role = WifiRole::None;
    /** For WlanClone: parent radio name (iwlwifi0). For ParentRadio: self. */
    std::string wifi_parent;
    /** Associated SSID when connected / configured (empty if none). */
    std::string wifi_ssid;
    /** Channel number from ifconfig (0 if unknown). */
    unsigned wifi_channel = 0;
    /**
     * Parent radio that still has no wlan(4) clone — UI may offer Create.
     * False for clones and for parents that already have at least one wlan.
     */
    bool wifi_needs_clone = false;
    /**
     * wpa_supplicant state when known: COMPLETED, ASSOCIATED, SCANNING,
     * DISCONNECTED, INACTIVE, … Empty if wpa not running / not queried.
     */
    std::string wifi_wpa_state;
    /** BSSID when associated (from ifconfig / wpa_cli). */
    std::string wifi_bssid;
    /**
     * Signal when associated: dBm if known (negative, e.g. -45), else 0.
     * FreeBSD ifconfig list sta RSSI is also stored as percent-ish in
     * wifi_signal_pct when dBm unavailable.
     */
    int wifi_signal_dbm = 0;
    /** 0 = unknown; 1–100 = relative strength for icons. */
    unsigned char wifi_signal_pct = 0;
};

/** Fingerprint for anti-flicker / skip-rebuild. */
inline std::string interface_fingerprint(const InterfaceInfo& i)
{
    std::string fp = i.name;
    fp += '\x1f';
    fp += i.up ? '1' : '0';
    fp += i.running ? '1' : '0';
    fp += i.is_default_route ? '1' : '0';
    fp += '\x1f';
    for (const auto& a : i.ipv4)
    {
        fp += a;
        fp += ',';
    }
    fp += '\x1f';
    for (const auto& a : i.ipv6)
    {
        fp += a;
        fp += ',';
    }
    fp += '\x1f';
    fp += i.is_default_route_v4 ? '4' : '-';
    fp += i.is_default_route_v6 ? '6' : '-';
    fp += '\x1f';
    fp += i.gateway_v4;
    fp += '\x1f';
    fp += i.gateway_v6;
    fp += '\x1f';
    fp += i.media;
    fp += '\x1f';
    fp += i.status;
    fp += '\x1f';
    fp += static_cast<char>('0' + static_cast<int>(i.wifi_role));
    fp += '\x1f';
    fp += i.wifi_parent;
    fp += '\x1f';
    fp += i.wifi_ssid;
    fp += '\x1f';
    fp += std::to_string(i.wifi_channel);
    fp += '\x1f';
    fp += i.wifi_needs_clone ? '1' : '0';
    fp += '\x1f';
    fp += i.wifi_wpa_state;
    fp += '\x1f';
    fp += i.wifi_bssid;
    fp += '\x1f';
    fp += std::to_string(i.wifi_signal_dbm);
    fp += '\x1f';
    fp += std::to_string(static_cast<unsigned>(i.wifi_signal_pct));
    return fp;
}

/**
 * Human Wi‑Fi connection state for list rows (pure).
 * e.g. "Connected", "Associated", "Disconnected", "Off", "No wlan".
 */
std::string format_wifi_connection_state(const InterfaceInfo& info);

/**
 * Address block for list: IPv4 then IPv6 (global first), one per line.
 * Shows up to max_addrs total. Empty if none. Pure.
 */
std::string format_address_summary(const InterfaceInfo& info, size_t max_addrs = 4);

inline const char *kind_label(InterfaceKind k)
{
    switch (k)
    {
        case InterfaceKind::Ethernet: return "Ethernet";
        case InterfaceKind::Wireless: return "Wireless";
        case InterfaceKind::Bridge:   return "Bridge";
        case InterfaceKind::Virtual:  return "Virtual";
        case InterfaceKind::Loopback: return "Loopback";
        default: return "Network";
    }
}

/**
 * Name-only structural classify (pure) — **no ethernet driver prefix table**.
 * Recognizes loopback / wlan / wifi parents / bridge / common clones by shape.
 * Unknown hardware NIC names (aq0, igb0, …) return Other; live probe upgrades
 * them via media/groups and dynamically discovered ethernet stems.
 */
InterfaceKind classify_iface_name(const std::string& name);

/**
 * Driver stem of a FreeBSD unit name (pure):
 *   aq0 → aq, igb12 → igb, epair0a → epair, vm-public → vm, wlan0 → wlan.
 * Empty if name has no stem.
 */
std::string iface_driver_stem(const std::string& name);

/**
 * Classify from ifconfig `media:` value (pure).
 * "Ethernet …" → Ethernet, "IEEE 802.11 …" → Wireless, else Other.
 */
InterfaceKind classify_from_media(const std::string& media);

/**
 * Classify from FreeBSD interface groups (pure).
 * Priority among groups: lo → Loopback, wlan → Wireless, bridge → Bridge,
 * tap/tun/epair/gif/gre/lagg/vlan/wg/vxlan → Virtual, ether → Ethernet.
 * Empty / unknown → Other.
 */
InterfaceKind classify_from_groups(const std::vector<std::string>& groups);

/**
 * Parse tokens from an ifconfig `groups: a b c` line body (after "groups:").
 * Pure.
 */
std::vector<std::string> parse_ifconfig_groups_field(const std::string& field);

/**
 * Full live classification (pure): groups + media + loopback flag + name + optional
 * dynamically discovered ethernet driver stems (from prior media:Ethernet ifaces).
 *
 * Priority: loopback flag/groups → wireless → bridge → virtual groups →
 * media Ethernet → name structural → ethernet_stems match → Other.
 */
InterfaceKind classify_iface(const std::string& name,
    const std::string& media,
    const std::vector<std::string>& groups,
    bool ifa_loopback = false,
    const std::vector<std::string>& ethernet_stems = {});

/**
 * True if name is a FreeBSD wlan(4) clone: wlan0, wlan1, … (pure).
 * These are the usable 802.11 stack interfaces.
 */
bool is_wlan_clone_name(const std::string& name);

/**
 * True if name matches a known FreeBSD 802.11 parent driver unit
 * (iwlwifi0, iwm0, ath0, rtw88_0-style, …) — pure. Not wlanN.
 */
bool is_wifi_parent_name(const std::string& name);

/**
 * Parse `sysctl -n net.wlan.devices` output into parent radio names.
 * Whitespace/comma separated. Pure. Empty/garbage → empty vector.
 */
std::vector<std::string> parse_wlan_devices_sysctl(const std::string& text);

/**
 * Next free wlanN name given already-seen wlan clones (pure).
 * existing may be unsorted; ignores non-wlan names.
 */
std::string next_wlan_clone_name(const std::vector<std::string>& existing_wlans);

/**
 * Build ifconfig create argv-style command line (no elevation prefix).
 * Pure. Empty if parent/wlan names are unsafe.
 * Example: ifconfig wlan0 create wlandev iwlwifi0
 */
std::string build_wlan_create_command(const std::string& wlan_name,
    const std::string& parent_name);

/**
 * Parents that still need a wlan clone (pure).
 * parents = net.wlan.devices; clone_parents = wifi_parent of each existing wlan.
 */
std::vector<std::string> parents_needing_wlan_clone(
    const std::vector<std::string>& parents,
    const std::vector<std::string>& clone_parents);

/**
 * Compact multi-line label for tray / list (pure).
 * Line 1: iface name [· default]
 * Then IPv4 and/or IPv6 each on their own line — only if present (no blank lines).
 */
std::string format_display_name(const InterfaceInfo& info);

/**
 * Human link speed from ifconfig media (e.g. "10Gbase-T <full-duplex>" → "10 Gbps").
 * Empty if unknown. Pure.
 */
std::string format_media_speed(const std::string& media);

/**
 * Wi‑Fi / NM bitrate in Kb/s → "1.2 Gbps" / "300 Mbps". Empty if 0.
 */
std::string format_bitrate_kbps(unsigned max_bitrate_kbps);

/**
 * Best available speed label for an interface list row:
 * Wi‑Fi uses max_bitrate if set, else media parse. Empty if unknown.
 */
std::string format_iface_speed(const InterfaceInfo& info);

/**
 * Byte count → sensible SI label: "392 B", "1.5 KB", "36.6 GB".
 * Pure.
 */
std::string format_byte_count(uint64_t bytes);

/**
 * Byte rate (bytes/sec) → sensible label: "120 B/s", "1.2 MB/s", "850 KB/s".
 * Pure.
 */
std::string format_byte_rate(uint64_t bytes_per_sec);

/**
 * Bit rate (bits/sec) from byte rate — network-style: "100 Mbps", "1.2 Gbps".
 * Pure. bytes_per_sec is converted ×8.
 */
std::string format_bit_rate_from_bytes(uint64_t bytes_per_sec);

/** Icon base name without -symbolic (pure). */
std::string icon_for_interface(const InterfaceInfo& info);

/** CSS classes for strength/type (pure). */
std::vector<std::string> css_for_interface(const InterfaceInfo& info);

/** Stable path for device maps. */
inline std::string path_for_iface(const std::string& name)
{
    return "/freebsd/interfaces/" + name;
}

/** Options for probing (Builder feeds these). */
struct ProbeOptions
{
    bool include_loopback = false;
    bool include_virtual  = true;  /**< tap/tun/epair */
    bool include_bridge   = true;
    bool include_down     = true;  /**< list interfaces that are down */
    /**
     * Include FreeBSD parent radios from net.wlan.devices even when they are
     * not yet real ifconfig interfaces (no wlan clone). Default true so Wi‑Fi
     * hardware is never invisible.
     */
    bool include_wifi_parents = true;
    /**
     * When true, probe_interfaces may create missing wlan clones (blocking).
     * Default **false** so the panel poll path never freezes; wifi_turn_on()
     * creates clones on a background thread instead.
     */
    bool auto_create_wlan = false;
    int  poll_interval_ms = 3000;
};

/**
 * Privilege to mutate network state (ifconfig/sysrc/create/destroy).
 * Read-only probe always works without this.
 */
enum class AdminPrivilege
{
    None,           /**< no root/doas/sudo — information-only UI */
    NeedsPassword,  /**< doas or sudo present, but -n fails (password required) */
    Root,           /**< already euid 0 */
    Doas,           /**< doas -n succeeds (nopass or already authenticated) */
    Sudo,           /**< sudo -n succeeds */
};

/** Stack capabilities for UI gating (autodetect — never invent). */
struct NetworkStackFeatures
{
    bool physical_ifaces = false;
    bool default_route   = false;
    bool wireless        = false;
    bool wpa             = false;
    /**
     * Mutations may be offered (root, nopass doas/sudo, or elevators that need a password).
     * False only when no elevators exist → information-only.
     */
    bool can_admin       = false;
    /**
     * True when doas/sudo exists but a password is required before each elevated action
     * (or until the ticket cache is warm). UI shows an auth dialog — not information-only.
     */
    bool needs_password  = false;
    AdminPrivilege admin = AdminPrivilege::None;
    /** Elevator name for dialogs/diagnostics: root | doas | sudo (empty if none). */
    std::string admin_method;
};

/** No elevators at all — list/tooltip only. */
inline bool is_information_only(const NetworkStackFeatures& f)
{
    return f.admin == AdminPrivilege::None || !f.can_admin;
}

/** Show password dialog before mutation (doas/sudo present, not yet authenticated). */
inline bool needs_admin_password(const NetworkStackFeatures& f)
{
    return f.needs_password || f.admin == AdminPrivilege::NeedsPassword;
}

/* ─── Clone / destroy / create preflight (pure; apply path deferred) ───── */

/**
 * True if `ifconfig NAME destroy` is a safe UI action for this name.
 * Permanent hardware NICs (aq0, igb0, …) and system lo0 are never destroyable.
 * Cloned types (tap, bridge, gif, vlan, lagg, epair, gre, …) are.
 * Pure — no I/O.
 */
bool is_destroyable_iface(const std::string& name);

/**
 * Label for the Turn on / Turn off menu item from live IFF_UP.
 * Pure: up → "Turn off", down → "Turn on".
 */
inline const char *toggle_action_label(bool iface_up)
{
    return iface_up ? "Turn off" : "Turn on";
}

/**
 * Resolve which name to act on for Wi‑Fi power (pure).
 * Parent radio → still that name (apply path creates clone).
 * wlan clone → itself. Empty/unknown → empty.
 */
inline std::string wifi_power_target_name(const InterfaceInfo& info)
{
    if (info.wifi_role == WifiRole::WlanClone || is_wlan_clone_name(info.name))
    {
        return info.name;
    }
    if (info.wifi_role == WifiRole::ParentRadio || is_wifi_parent_name(info.name))
    {
        return info.name;
    }
    if (info.kind == InterfaceKind::Wireless)
    {
        return info.name;
    }
    return {};
}

/** One FreeBSD cloned-interface type offered in Create… (static catalog). */
struct CloneTypeInfo
{
    const char *type;          /**< ifconfig type token: tap, bridge, gif, … */
    const char *label;         /**< UI label */
    const char *module;        /**< kld name without .ko, or nullptr if usually in-kernel */
    bool destroyable = true;   /**< instances of this type may be destroyed */
};

/** Static catalog of types we surface in Create… (subset of ifconfig -C). */
const CloneTypeInfo *known_clone_types(size_t *count_out);

/** Look up a type in the static catalog; nullptr if unknown. */
const CloneTypeInfo *find_clone_type(const std::string& type);

/**
 * Parse `ifconfig -C` output (space-separated type names on one line).
 * Pure — unit-testable.
 */
std::vector<std::string> parse_ifconfig_clone_list(const std::string& text);

/**
 * True if `kldstat` text shows module loaded (matches "module.ko" or bare name).
 * Pure.
 */
bool kldstat_has_module(const std::string& kldstat_text, const std::string& module_name);

/**
 * Result of create preflight (gate only).
 * UI: if can_create, present the type as editable — no success narration.
 * detail is diagnostics only (logs/tests), never a green “module OK” banner.
 */
struct CreatePreflight
{
    bool can_create = false;
    std::string type;
    std::string module;   /**< empty if none required / unknown */
    std::string detail;   /**< fail reason for logs; empty when can_create */
};

/**
 * Pure preflight decision for Create….
 *
 * @param type              clone type (tap, gif, …)
 * @param clone_catalog     types currently offered by `ifconfig -C`
 * @param module_loaded     kldstat shows module (true if no module needed)
 * @param module_file_exists .ko present under /boot/kernel or /boot/modules
 * @param has_admin         root/doas/sudo available
 */
CreatePreflight evaluate_create_preflight(
    const std::string& type,
    const std::vector<std::string>& clone_catalog,
    bool module_loaded,
    bool module_file_exists,
    bool has_admin);

/* ─── Input validation (pure; UI blocks save/create until ok) ─────────── */

/** One field check: ok + short user message (never shell/command text). */
struct ValidationResult
{
    bool ok = true;
    std::string message;
};

inline ValidationResult validation_ok()
{
    return ValidationResult{true, {}};
}

inline ValidationResult validation_fail(const std::string& message)
{
    return ValidationResult{false, message};
}

/**
 * FreeBSD interface name: 1…15 chars (IFNAMSIZ-1), starts with letter,
 * then alnum / _ / - / .  Empty allowed when allow_empty (Create “auto”).
 */
ValidationResult validate_iface_name(const std::string& name, bool allow_empty = false);

/** Dotted IPv4 (e.g. 192.168.1.10). Empty → fail unless allow_empty. */
ValidationResult validate_ipv4_address(const std::string& text, bool allow_empty = false);

/**
 * IPv6 address; optional zone id after '%' (fe80::1%aq0).
 * Empty → fail unless allow_empty.
 */
ValidationResult validate_ipv6_address(const std::string& text, bool allow_empty = false);

/** Decimal prefix length in [0, max_bits] (32 or 128). */
ValidationResult validate_prefix_length(const std::string& text, int max_bits,
    bool allow_empty = false);

/** Auth dialog: non-empty, length cap (no format rules beyond that). */
ValidationResult validate_admin_password(const std::string& password);

/* ─── Wi‑Fi credentials (wpa_supplicant) ──────────────────────────────── */

/** One row from `wpa_cli scan_results` (pure domain type). */
struct WifiScanEntry
{
    std::string bssid;
    unsigned freq_mhz = 0;
    int signal_dbm = 0;       /**< typically negative dBm */
    std::string flags;        /**< e.g. [WPA2-PSK-CCMP][ESS] */
    std::string ssid;
    /** Derived: "open" | "wpa" | "wep" | "sae" */
    std::string security = "open";
};

/**
 * Parse `wpa_cli scan_results` body (with or without header line). Pure.
 * Skips empty SSIDs and P2P-only rows. Best signal wins per SSID.
 */
std::vector<WifiScanEntry> parse_wpa_scan_results(const std::string& text);

/** Map wpa flags string → security token (pure). */
std::string wifi_security_from_flags(const std::string& flags);

/** dBm → 0…100 strength (pure; clamps). 0 dBm treated as unknown → 0. */
unsigned char wifi_signal_to_percent(int signal_dbm);

/**
 * FreeBSD `ifconfig wlan list sta` RSSI (often 0–100 relative) → percent.
 * Pure. Values already in 1–100 pass through; 0 unknown.
 */
unsigned char wifi_rssi_to_percent(double rssi);

/** Adwaita base icon name from percent (no -symbolic). Pure. */
std::string wifi_signal_icon_base(unsigned char percent);

/**
 * Parse `ifconfig wlanN list sta` body; first station RSSI into *rssi_out.
 * Pure. false if no station row.
 */
bool parse_ifconfig_list_sta_rssi(const std::string& text, double *rssi_out);

/**
 * Parse `wpa_cli bss …` / signal_poll body for level= (dBm).
 * Pure. false if missing.
 */
bool parse_wpa_signal_level(const std::string& text, int *dbm_out);

/** SSID: 1…32 octets (FreeBSD/IEEE 802.11). */
ValidationResult validate_wifi_ssid(const std::string& ssid);

/**
 * WPA/WPA2/WPA3-Personal passphrase: 8–63 characters, or 64 hex digits (PSK).
 * Empty fails.
 */
ValidationResult validate_wifi_wpa_psk(const std::string& key);

/**
 * WEP key: 5 or 13 ASCII characters, or 10/26 hex digits.
 */
ValidationResult validate_wifi_wep_key(const std::string& key);

/**
 * security: "open" | "wpa" | "wep"
 * Open ignores key. WPA/WEP validate key format.
 */
ValidationResult validate_wifi_credentials(const std::string& security,
    const std::string& key);

/** Configure modal field bundle (modes: dhcp|static|none / accept_rtadv|static|none). */
struct ConfigFormInput
{
    std::string v4_mode = "dhcp";
    std::string v4_addr;
    std::string v4_prefix;
    std::string v4_gateway;
    std::string v6_mode = "accept_rtadv";
    std::string v6_addr;
    std::string v6_prefix;
    std::string v6_gateway;
};

/** Per-field messages; empty string = that field is fine. */
struct ConfigFormErrors
{
    bool ok = true;
    std::string v4_addr;
    std::string v4_prefix;
    std::string v4_gateway;
    std::string v6_addr;
    std::string v6_prefix;
    std::string v6_gateway;
};

ConfigFormErrors validate_config_form(const ConfigFormInput& in);

/**
 * Create modal: type must be known; name empty (auto) or valid + not taken.
 * @param existing_names  current interface names (case-sensitive)
 */
struct CreateFormInput
{
    std::string type;
    std::string name; /**< empty = auto-assign */
};

struct CreateFormErrors
{
    bool ok = true;
    std::string type;
    std::string name;
};

CreateFormErrors validate_create_form(const CreateFormInput& in,
    const std::vector<std::string>& existing_names);

/**
 * Wi‑Fi RF helpers (centre frequency in MHz from NM / wpa / ifconfig).
 * Pure — unit-testable. Empty string if unknown.
 *
 * @param freq_mhz     AP centre frequency (MHz), e.g. 2412, 5180
 * @param max_bitrate_kbps  NM MaxBitrate (Kb/s); 0 if unknown — generation is best-effort
 */
std::string format_wifi_frequency_mhz(unsigned freq_mhz);
/** Band: "2.4 GHz", "5 GHz", "6 GHz", … */
std::string format_wifi_band(unsigned freq_mhz);
/**
 * Marketing generation: "Wi-Fi 4" … "Wi-Fi 7", "Wi-Fi 6E".
 * Inferred from band + optional MaxBitrate (not perfect without HE/EHT IE).
 */
std::string format_wifi_generation(unsigned freq_mhz, unsigned max_bitrate_kbps = 0);
/**
 * Compact list/tray radio line, e.g. "5 GHz · Wi-Fi 6".
 * Only includes parts that are known — no "???" placeholders.
 */
std::string format_wifi_radio_label(unsigned freq_mhz, unsigned max_bitrate_kbps = 0);

} // namespace wf_net
