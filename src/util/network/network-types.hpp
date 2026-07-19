#pragma once

/**
 * Application-agnostic network domain types (TAOCP-style clear data).
 * UI and FreeBSD/Linux backends translate live OS state into these.
 */

#include <string>
#include <vector>
#include <cstdint>

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
    std::string media;     /**< e.g. "10Gbase-T <full-duplex>" */
    std::string status;    /**< ifconfig status: active / no carrier */
    bool is_default_route = false;    /**< IPv4 and/or IPv6 default route egress */
    bool is_default_route_v4 = false;
    bool is_default_route_v6 = false;
    std::string gateway_v4;  /**< from route get default (may be empty) */
    std::string gateway_v6;
    /** Stable path key for maps (not a real D-Bus path on FreeBSD). */
    std::string path;
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
    return fp;
}

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

/** Classify FreeBSD interface name / groups-ish heuristics (pure). */
InterfaceKind classify_iface_name(const std::string& name);

/**
 * Compact multi-line label for tray / list (pure).
 * Line 1: iface name [· default]
 * Then IPv4 and/or IPv6 each on their own line — only if present (no blank lines).
 */
std::string format_display_name(const InterfaceInfo& info);

/**
 * Address lines only: IPv4 then IPv6, newline-separated, omit missing.
 * Empty string if neither is present.
 */
std::string format_address_summary(const InterfaceInfo& info);

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
    int  poll_interval_ms = 3000;
};

/**
 * Privilege to mutate network state (ifconfig/sysrc/create/destroy).
 * Read-only probe always works without this.
 */
enum class AdminPrivilege
{
    None,   /**< information-only UI */
    Root,   /**< already euid 0 */
    Doas,   /**< doas -n succeeds (passwordless or already authenticated) */
    Sudo,   /**< sudo -n succeeds */
};

/** Stack capabilities for UI gating (autodetect — never invent). */
struct NetworkStackFeatures
{
    bool physical_ifaces = false;
    bool default_route   = false;
    bool wireless        = false;
    bool wpa             = false;
    /** FreeBSD config editor / create / destroy / up-down require admin. */
    bool can_admin       = false;
    AdminPrivilege admin = AdminPrivilege::None;
    /** How admin was detected (for diagnostics; empty if none). */
    std::string admin_method;
};

inline bool is_information_only(const NetworkStackFeatures& f)
{
    return !f.can_admin;
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

/** Result of create preflight (UI gate + reason string). */
struct CreatePreflight
{
    bool can_create = false;
    std::string type;
    std::string module;   /**< empty if none required / unknown */
    std::string detail;   /**< human-readable reason for UI strip */
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
