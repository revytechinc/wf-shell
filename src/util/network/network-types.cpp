#include "network-types.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace wf_net
{

InterfaceKind classify_iface_name(const std::string& name)
{
    if (name.empty())
    {
        return InterfaceKind::Other;
    }
    /* Loopback */
    if (name == "lo0" || (name.size() >= 2 && name[0] == 'l' && name[1] == 'o' &&
            (name.size() == 2 || std::isdigit(static_cast<unsigned char>(name[2])))))
    {
        return InterfaceKind::Loopback;
    }
    /* FreeBSD wlan(4) */
    if (name.rfind("wlan", 0) == 0)
    {
        return InterfaceKind::Wireless;
    }
    /* Bridges / switches */
    if (name.rfind("bridge", 0) == 0 || name.find("vm-public") != std::string::npos ||
        name.rfind("vm-", 0) == 0)
    {
        /* vm-public is bridge group; vm-port is tap — check tap first below */
        if (name.rfind("bridge", 0) == 0 || name == "vm-public")
        {
            return InterfaceKind::Bridge;
        }
    }
    /* Virtual NICs */
    if (name.rfind("tap", 0) == 0 || name.rfind("tun", 0) == 0 ||
        name.rfind("epair", 0) == 0 || name.rfind("vnet", 0) == 0 ||
        name.rfind("wg", 0) == 0 || name.rfind("gif", 0) == 0 ||
        name.rfind("gre", 0) == 0 || name.rfind("lagg", 0) == 0)
    {
        return InterfaceKind::Virtual;
    }
    /* Common Ethernet drivers + generic */
    static const char *eth_prefixes[] = {
        "em", "igb", "ix", "ixl", "aq", "re", "rl", "bge", "bnx", "bnxt",
        "cxgbe", "oce", "qlxgb", "alc", "ale", "age", "msk", "nfe", "stge",
        "vge", "vr", "xl", "fxp", "dc", "le", "ue", "axe", "cdce", "ure",
        "if_bridge", "ether", "eth", "en", "vtnet", "xn", "virtio",
        nullptr
    };
    for (int i = 0; eth_prefixes[i]; i++)
    {
        const char *p = eth_prefixes[i];
        size_t n = std::char_traits<char>::length(p);
        if (name.rfind(p, 0) == 0 && name.size() > n &&
            std::isdigit(static_cast<unsigned char>(name[n])))
        {
            return InterfaceKind::Ethernet;
        }
    }
    /* bastille0 etc. loopback-like jails */
    if (name.rfind("bastille", 0) == 0)
    {
        return InterfaceKind::Loopback;
    }
    return InterfaceKind::Other;
}

std::string format_address_summary(const InterfaceInfo& info)
{
    /* Separate lines, only when present — no trailing/leading blanks. */
    std::string s;
    if (!info.ipv4.empty())
    {
        s = info.ipv4.front();
    }
    if (!info.ipv6.empty())
    {
        if (!s.empty())
        {
            s += '\n';
        }
        s += info.ipv6.front();
    }
    return s;
}

std::string format_display_name(const InterfaceInfo& info)
{
    /* Compact panel label:
     *   aq0 · default
     *   99.48.162.238
     *   2600:1700:…
     */
    std::string base = info.name;
    if (info.is_default_route)
    {
        base += " · default";
    } else if (!info.running && info.ipv4.empty() && info.ipv6.empty())
    {
        base += " · offline";
    }

    std::string addrs = format_address_summary(info);
    if (!addrs.empty())
    {
        base += '\n';
        base += addrs;
    }
    return base;
}

std::string icon_for_interface(const InterfaceInfo& info)
{
    bool active = info.up && info.running;
    switch (info.kind)
    {
        case InterfaceKind::Wireless:
            /* Theme: network-wireless-signal-{excellent,good,ok,weak,none}-symbolic
             * and network-wireless-offline-symbolic. No "disconnected" name. */
            return active ? "network-wireless-signal-excellent" : "network-wireless-offline";
        case InterfaceKind::Bridge:
            /* network-workgroup is places-only on some themes and often blank in tray */
            return active ? "network-server" : "network-offline";
        case InterfaceKind::Virtual:
            /* tap/tun/epair — transmit-receive is widely available as status/legacy */
            return active ? "network-transmit-receive" : "network-offline";
        case InterfaceKind::Ethernet:
        case InterfaceKind::Other:
        default:
            return active ? "network-wired" : "network-wired-disconnected";
    }
}

std::vector<std::string> css_for_interface(const InterfaceInfo& info)
{
    std::vector<std::string> c;
    switch (info.kind)
    {
        case InterfaceKind::Wireless: c.push_back("wifi"); break;
        case InterfaceKind::Ethernet: c.push_back("ethernet"); break;
        case InterfaceKind::Bridge:   c.push_back("ethernet"); break;
        case InterfaceKind::Virtual:  c.push_back("ethernet"); break;
        default: c.push_back("none"); break;
    }
    if (info.up && info.running)
    {
        c.push_back(info.is_default_route ? "excellent" : "good");
    } else if (info.up)
    {
        c.push_back("medium");
    } else
    {
        c.push_back("none");
    }
    return c;
}

std::string format_wifi_frequency_mhz(unsigned freq_mhz)
{
    if (freq_mhz == 0)
    {
        return {};
    }
    /* NM and most stacks report centre frequency in MHz (e.g. 2412, 5180). */
    return std::to_string(freq_mhz) + " MHz";
}

std::string format_wifi_band(unsigned freq_mhz)
{
    if (freq_mhz == 0)
    {
        return {};
    }
    if ((freq_mhz > 800) && (freq_mhz < 1000))
    {
        return "900 MHz";
    }
    if ((freq_mhz > 2000) && (freq_mhz < 3000))
    {
        return "2.4 GHz";
    }
    if ((freq_mhz >= 5000) && (freq_mhz < 6000))
    {
        return "5 GHz";
    }
    if ((freq_mhz >= 6000) && (freq_mhz < 7000))
    {
        return "6 GHz";
    }
    if ((freq_mhz >= 40000) && (freq_mhz < 50000))
    {
        return "45 GHz";
    }
    if ((freq_mhz >= 57000) && (freq_mhz < 74000))
    {
        return "60 GHz";
    }
    return {};
}

std::string format_wifi_generation(unsigned freq_mhz, unsigned max_bitrate_kbps)
{
    if (freq_mhz == 0 && max_bitrate_kbps == 0)
    {
        return {};
    }

    const bool band_6g = (freq_mhz >= 6000 && freq_mhz < 7000);
    const bool band_5g = (freq_mhz >= 5000 && freq_mhz < 6000);
    const bool band_24 = (freq_mhz > 2000 && freq_mhz < 3000);

    /*
     * Heuristic (no HE/EHT IE on all stacks):
     * MaxBitrate is Kb/s from NetworkManager AccessPoint.
     * Thresholds are approximate ceilings for each generation.
     *
     * Wi-Fi 7 (802.11be / EHT): very high advertised rates
     * Wi-Fi 6/6E (802.11ax / HE): high rates; 6 GHz ⇒ 6E
     * Wi-Fi 5 (802.11ac / VHT): mid-high, typically 5 GHz
     * Wi-Fi 4 (802.11n / HT): lower; common on 2.4 GHz
     * Wi-Fi 3 (802.11g): ~54 Mb/s class on 2.4 GHz
     */
    if (max_bitrate_kbps >= 2000000) /* ≥ ~2 Gb/s */
    {
        return band_6g ? "Wi-Fi 7" : "Wi-Fi 7";
    }
    if (max_bitrate_kbps >= 600000 || (band_6g && max_bitrate_kbps >= 100000))
    {
        return band_6g ? "Wi-Fi 6E" : "Wi-Fi 6";
    }
    if (band_6g)
    {
        /* 6 GHz is only 6E/7 — default 6E without strong bitrate */
        return "Wi-Fi 6E";
    }
    if (max_bitrate_kbps >= 200000 || (band_5g && max_bitrate_kbps >= 100000))
    {
        return "Wi-Fi 5";
    }
    /* 802.11n (Wi-Fi 4): above classic 54 Mb/s class */
    if (max_bitrate_kbps > 54000)
    {
        return "Wi-Fi 4";
    }
    /* 802.11g (Wi-Fi 3): up to ~54 Mb/s on 2.4 GHz */
    if (max_bitrate_kbps > 0 && band_24)
    {
        return "Wi-Fi 3";
    }
    /* Band-only fallback when bitrate missing */
    if (band_5g)
    {
        return "Wi-Fi 5";
    }
    if (band_24)
    {
        return "Wi-Fi 4";
    }
    return {};
}

std::string format_wifi_radio_label(unsigned freq_mhz, unsigned max_bitrate_kbps)
{
    std::string band = format_wifi_band(freq_mhz);
    std::string gen  = format_wifi_generation(freq_mhz, max_bitrate_kbps);

    if (!band.empty() && !gen.empty())
    {
        return band + " · " + gen;
    }
    if (!band.empty())
    {
        return band;
    }
    if (!gen.empty())
    {
        return gen;
    }
    return format_wifi_frequency_mhz(freq_mhz);
}

/* ─── Clone / destroy / create preflight ─────────────────────────────────── */

namespace
{

/** Return true if name is PREFIX + digits (e.g. tap0, bridge12). */
bool is_prefix_unit(const std::string& name, const char *prefix)
{
    const size_t n = std::char_traits<char>::length(prefix);
    if (name.size() <= n || name.rfind(prefix, 0) != 0)
    {
        return false;
    }
    for (size_t i = n; i < name.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(name[i])))
        {
            return false;
        }
    }
    return true;
}

/** Permanent hardware / system NICs — never ifconfig destroy from the panel. */
bool is_permanent_hardware_name(const std::string& name)
{
    if (name == "lo0")
    {
        return true;
    }
    static const char *eth_prefixes[] = {
        "em", "igb", "ix", "ixl", "aq", "re", "rl", "bge", "bnx", "bnxt",
        "cxgbe", "oce", "qlxgb", "alc", "ale", "age", "msk", "nfe", "stge",
        "vge", "vr", "xl", "fxp", "dc", "le", "ue", "axe", "cdce", "ure",
        "eth", "en", "vtnet", "xn", "bce", "bfe", "cas", "cc", "cxgb",
        "ena", "enic", "et", "ice", "igc", "ixv", "jme", "lge", "liquidio",
        "mlx", "mlxen", "mthca", "mxge", "myri", "nfe", "nge", "nxe",
        "oce", "qlnxe", "qlxge", "ral", "rdma", "sge", "siba", "sge",
        "sk", "ste", "stge", "ti", "txp", "vte", "xl", "atlantic",
        nullptr
    };
    for (int i = 0; eth_prefixes[i]; ++i)
    {
        if (is_prefix_unit(name, eth_prefixes[i]))
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool is_destroyable_iface(const std::string& name)
{
    if (name.empty() || is_permanent_hardware_name(name))
    {
        return false;
    }
    /* Cloned / virtual families FreeBSD creates with ifconfig TYPE create */
    static const char *clone_prefixes[] = {
        "tap", "tun", "bridge", "gif", "gre", "vlan", "lagg", "epair",
        "vxlan", "stf", "wg", "lo", "vmnet", "usbus",
        nullptr
    };
    for (int i = 0; clone_prefixes[i]; ++i)
    {
        if (is_prefix_unit(name, clone_prefixes[i]))
        {
            /* lo0 already rejected; lo1+ clones ok */
            return true;
        }
    }
    /* epair ends in a/b: epair0a, epair0b */
    if (name.rfind("epair", 0) == 0 && name.size() > 5)
    {
        return true;
    }
    /* bhyve / custom: vm-public (bridge group), vm-port* (tap) */
    if (name.rfind("vm-", 0) == 0)
    {
        return true;
    }
    /* Cloned wlan (ifconfig wlan create) — not permanent radio parent */
    if (is_prefix_unit(name, "wlan"))
    {
        return true;
    }
    return false;
}

const CloneTypeInfo *known_clone_types(size_t *count_out)
{
    static const CloneTypeInfo kTypes[] = {
        {"tap",    "tap (virtual Ethernet)", "if_tuntap", true},
        {"tun",    "tun (point-to-point)",   "if_tuntap", true},
        {"bridge", "bridge",                 "if_bridge", true},
        {"gif",    "gif (IPv6-in-IPv4 / tunnel)", "if_gif", true},
        {"gre",    "gre (tunnel)",           "if_gre", true},
        {"vlan",   "vlan",                   "if_vlan", true},
        {"lagg",   "lagg (aggregation)",     "if_lagg", true},
        {"epair",  "epair (pair)",           "if_epair", true},
        {"vxlan",  "vxlan",                  "if_vxlan", true},
        {"wg",     "wg (WireGuard)",         "if_wg", true},
        {"lo",     "lo (loopback clone)",    nullptr, true},
        {"stf",    "stf (6to4)",             "if_stf", true},
    };
    if (count_out)
    {
        *count_out = sizeof(kTypes) / sizeof(kTypes[0]);
    }
    return kTypes;
}

const CloneTypeInfo *find_clone_type(const std::string& type)
{
    size_t n = 0;
    const CloneTypeInfo *t = known_clone_types(&n);
    for (size_t i = 0; i < n; ++i)
    {
        if (type == t[i].type)
        {
            return &t[i];
        }
    }
    return nullptr;
}

std::vector<std::string> parse_ifconfig_clone_list(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(ch);
        }
    }
    if (!cur.empty())
    {
        out.push_back(cur);
    }
    return out;
}

bool kldstat_has_module(const std::string& kldstat_text, const std::string& module_name)
{
    if (module_name.empty() || kldstat_text.empty())
    {
        return false;
    }
    /* Match "if_gif.ko" or bare "if_gif" as a token */
    const std::string with_ko = module_name + ".ko";
    auto contains_token = [&] (const std::string& needle) {
        size_t pos = 0;
        while ((pos = kldstat_text.find(needle, pos)) != std::string::npos)
        {
            const bool left_ok = (pos == 0) ||
                (!std::isalnum(static_cast<unsigned char>(kldstat_text[pos - 1])) &&
                 kldstat_text[pos - 1] != '_');
            const size_t end = pos + needle.size();
            const bool right_ok = (end >= kldstat_text.size()) ||
                (!std::isalnum(static_cast<unsigned char>(kldstat_text[end])) &&
                 kldstat_text[end] != '_');
            if (left_ok && right_ok)
            {
                return true;
            }
            pos = end;
        }
        return false;
    };
    return contains_token(with_ko) || contains_token(module_name);
}

CreatePreflight evaluate_create_preflight(
    const std::string& type,
    const std::vector<std::string>& clone_catalog,
    bool module_loaded,
    bool module_file_exists,
    bool has_admin)
{
    CreatePreflight r;
    r.type = type;
    const CloneTypeInfo *spec = find_clone_type(type);
    if (!spec)
    {
        r.detail = "unknown interface type";
        return r;
    }
    if (spec->module)
    {
        r.module = spec->module;
    }
    if (!has_admin)
    {
        r.detail = "no admin privileges (information-only) — cannot create " + type;
        return r;
    }

    const bool in_catalog = std::find(clone_catalog.begin(), clone_catalog.end(), type)
        != clone_catalog.end();

    /*
     * Module not required (nullptr) or already offered by ifconfig -C:
     * kernel already knows the type → create is allowed.
     */
    if (!spec->module)
    {
        if (in_catalog || clone_catalog.empty())
        {
            /* empty catalog = caller skipped live -C (unit test / optimistic) */
            r.can_create = true;
            r.detail = std::string("in kernel / no module required · ifconfig ") +
                type + " create";
            return r;
        }
        r.detail = "type " + type + " not offered by ifconfig -C";
        return r;
    }

    if (in_catalog)
    {
        r.can_create = true;
        r.detail = std::string("module ") + spec->module +
            " available · ifconfig " + type + " create";
        return r;
    }

    if (module_loaded)
    {
        r.can_create = true;
        r.detail = std::string("module ") + spec->module +
            " loaded · ifconfig " + type + " create";
        return r;
    }

    if (module_file_exists)
    {
        /*
         * Module on disk but not in -C yet — create/kldload may still work
         * under admin. Allow Create; apply path will load or fail soft.
         */
        r.can_create = true;
        r.detail = std::string("module ") + spec->module +
            ".ko present (not loaded) · ifconfig " + type + " create";
        return r;
    }

    r.detail = std::string("module ") + spec->module +
        " not loaded and not found — cannot create " + type;
    return r;
}

} // namespace wf_net
