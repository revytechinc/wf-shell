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
        r.detail = "unknown_type";
        return r;
    }
    if (spec->module)
    {
        r.module = spec->module;
    }
    if (!has_admin)
    {
        r.detail = "no_admin";
        return r;
    }

    const bool in_catalog = std::find(clone_catalog.begin(), clone_catalog.end(), type)
        != clone_catalog.end();

    /*
     * Success ⇒ can_create only (detail stays empty).
     * UI presents the type; that is the signal that preflight passed.
     */
    if (!spec->module)
    {
        if (in_catalog || clone_catalog.empty())
        {
            /* empty catalog = caller skipped live -C (unit test / optimistic) */
            r.can_create = true;
            return r;
        }
        r.detail = "not_in_catalog";
        return r;
    }

    if (in_catalog || module_loaded || module_file_exists)
    {
        r.can_create = true;
        return r;
    }

    r.detail = "module_unavailable";
    return r;
}

/* ─── Input validation ───────────────────────────────────────────────────── */

namespace
{

std::string trim_copy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    {
        --b;
    }
    return s.substr(a, b - a);
}

bool is_hex_digit(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

ValidationResult validate_iface_name(const std::string& name, bool allow_empty)
{
    const std::string s = trim_copy(name);
    if (s.empty())
    {
        return allow_empty ? validation_ok() : validation_fail("Name is required");
    }
    /* FreeBSD IFNAMSIZ is 16 including NUL */
    if (s.size() > 15)
    {
        return validation_fail("Name is too long (max 15 characters)");
    }
    if (!std::isalpha(static_cast<unsigned char>(s[0])))
    {
        return validation_fail("Name must start with a letter");
    }
    for (char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-' || c == '.')
        {
            continue;
        }
        return validation_fail("Name has invalid characters");
    }
    /* Block shell / path injection shapes */
    if (s.find("..") != std::string::npos || s.find('/') != std::string::npos)
    {
        return validation_fail("Name has invalid characters");
    }
    return validation_ok();
}

ValidationResult validate_ipv4_address(const std::string& text, bool allow_empty)
{
    const std::string s = trim_copy(text);
    if (s.empty())
    {
        return allow_empty ? validation_ok() : validation_fail("IPv4 address is required");
    }
    int parts = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (parts > 0)
        {
            if (s[i] != '.')
            {
                return validation_fail("Invalid IPv4 address");
            }
            ++i;
        }
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
        {
            return validation_fail("Invalid IPv4 address");
        }
        int val = 0;
        int digits = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        {
            val = val * 10 + (s[i] - '0');
            ++digits;
            ++i;
            if (digits > 3 || val > 255)
            {
                return validation_fail("Invalid IPv4 address");
            }
        }
        if (digits == 0)
        {
            return validation_fail("Invalid IPv4 address");
        }
        ++parts;
    }
    if (parts != 4)
    {
        return validation_fail("Invalid IPv4 address");
    }
    return validation_ok();
}

ValidationResult validate_ipv6_address(const std::string& text, bool allow_empty)
{
    std::string s = trim_copy(text);
    if (s.empty())
    {
        return allow_empty ? validation_ok() : validation_fail("IPv6 address is required");
    }
    /* Optional zone: addr%ifname */
    const size_t pct = s.find('%');
    if (pct != std::string::npos)
    {
        const std::string zone = s.substr(pct + 1);
        s = s.substr(0, pct);
        if (s.empty())
        {
            return validation_fail("Invalid IPv6 address");
        }
        if (!validate_iface_name(zone, false).ok)
        {
            return validation_fail("Invalid IPv6 zone id");
        }
    }
    if (s.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos)
    {
        return validation_fail("Invalid IPv6 address");
    }
    if (s.find(':') == std::string::npos)
    {
        return validation_fail("Invalid IPv6 address");
    }

    /* At most one "::" compression */
    const size_t dc = s.find("::");
    const bool compressed = (dc != std::string::npos);
    if (compressed && s.find("::", dc + 2) != std::string::npos)
    {
        return validation_fail("Invalid IPv6 address");
    }
    /* No single leading/trailing colon unless part of :: */
    if (!compressed)
    {
        if (s.front() == ':' || s.back() == ':')
        {
            return validation_fail("Invalid IPv6 address");
        }
    }

    auto valid_hextet = [] (const std::string& part) -> bool {
        if (part.empty() || part.size() > 4)
        {
            return false;
        }
        for (char c : part)
        {
            if (!is_hex_digit(c))
            {
                return false;
            }
        }
        return true;
    };

    auto count_side = [&] (const std::string& side) -> int {
        if (side.empty())
        {
            return 0;
        }
        int n = 0;
        size_t start = 0;
        for (size_t i = 0; i <= side.size(); ++i)
        {
            if (i == side.size() || side[i] == ':')
            {
                const std::string part = side.substr(start, i - start);
                if (part.find('.') != std::string::npos)
                {
                    if (!validate_ipv4_address(part, false).ok)
                    {
                        return -1;
                    }
                    n += 2;
                }
                else
                {
                    if (!valid_hextet(part))
                    {
                        return -1;
                    }
                    ++n;
                }
                start = i + 1;
            }
        }
        return n;
    };

    int groups = 0;
    if (compressed)
    {
        const std::string left = s.substr(0, dc);
        const std::string right = s.substr(dc + 2);
        const int L = count_side(left);
        const int R = count_side(right);
        if (L < 0 || R < 0)
        {
            return validation_fail("Invalid IPv6 address");
        }
        groups = L + R;
        if (groups > 7)
        {
            return validation_fail("Invalid IPv6 address");
        }
    }
    else
    {
        groups = count_side(s);
        if (groups != 8)
        {
            return validation_fail("Invalid IPv6 address");
        }
    }
    return validation_ok();
}

ValidationResult validate_prefix_length(const std::string& text, int max_bits,
    bool allow_empty)
{
    const std::string s = trim_copy(text);
    if (s.empty())
    {
        return allow_empty ? validation_ok() : validation_fail("Prefix length is required");
    }
    if (max_bits <= 0 || max_bits > 128)
    {
        return validation_fail("Invalid prefix length");
    }
    if (s.size() > 3)
    {
        return validation_fail("Invalid prefix length");
    }
    int val = 0;
    for (char c : s)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return validation_fail("Invalid prefix length");
        }
        val = val * 10 + (c - '0');
        if (val > max_bits)
        {
            return validation_fail("Prefix must be 0–" + std::to_string(max_bits));
        }
    }
    return validation_ok();
}

ValidationResult validate_admin_password(const std::string& password)
{
    if (password.empty())
    {
        return validation_fail("Password is required");
    }
    if (password.size() > 512)
    {
        return validation_fail("Password is too long");
    }
    return validation_ok();
}

ConfigFormErrors validate_config_form(const ConfigFormInput& in)
{
    ConfigFormErrors e;
    if (in.v4_mode == "static")
    {
        auto a = validate_ipv4_address(in.v4_addr, false);
        if (!a.ok)
        {
            e.ok = false;
            e.v4_addr = a.message;
        }
        auto p = validate_prefix_length(in.v4_prefix, 32, false);
        if (!p.ok)
        {
            e.ok = false;
            e.v4_prefix = p.message;
        }
        auto g = validate_ipv4_address(in.v4_gateway, true);
        if (!g.ok)
        {
            e.ok = false;
            e.v4_gateway = g.message;
        }
    }
    else if (in.v4_mode != "dhcp" && in.v4_mode != "none")
    {
        e.ok = false;
        e.v4_addr = "Invalid IPv4 mode";
    }

    if (in.v6_mode == "static")
    {
        auto a = validate_ipv6_address(in.v6_addr, false);
        if (!a.ok)
        {
            e.ok = false;
            e.v6_addr = a.message;
        }
        auto p = validate_prefix_length(in.v6_prefix, 128, false);
        if (!p.ok)
        {
            e.ok = false;
            e.v6_prefix = p.message;
        }
        auto g = validate_ipv6_address(in.v6_gateway, true);
        if (!g.ok)
        {
            e.ok = false;
            e.v6_gateway = g.message;
        }
    }
    else if (in.v6_mode != "accept_rtadv" && in.v6_mode != "none")
    {
        e.ok = false;
        e.v6_addr = "Invalid IPv6 mode";
    }
    return e;
}

CreateFormErrors validate_create_form(const CreateFormInput& in,
    const std::vector<std::string>& existing_names)
{
    CreateFormErrors e;
    if (!find_clone_type(in.type))
    {
        e.ok = false;
        e.type = "Choose an interface type";
    }
    auto n = validate_iface_name(in.name, true);
    if (!n.ok)
    {
        e.ok = false;
        e.name = n.message;
    }
    else if (!trim_copy(in.name).empty())
    {
        const std::string name = trim_copy(in.name);
        for (const auto& ex : existing_names)
        {
            if (ex == name)
            {
                e.ok = false;
                e.name = "Name is already in use";
                break;
            }
        }
    }
    return e;
}

} // namespace wf_net
