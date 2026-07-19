#include "network-types.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace wf_net
{

namespace
{

/** True if name is PREFIX + unit number (digits), optional single trailing letter (epair0a). */
bool is_prefix_unit_local(const std::string& name, const char *prefix)
{
    if (!prefix || !*prefix)
    {
        return false;
    }
    const size_t n = std::char_traits<char>::length(prefix);
    if (name.size() <= n || name.rfind(prefix, 0) != 0)
    {
        return false;
    }
    size_t i = n;
    if (!std::isdigit(static_cast<unsigned char>(name[i])))
    {
        return false;
    }
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])))
    {
        ++i;
    }
    /* Allow optional single trailing letter (rare); reject junk. */
    if (i < name.size())
    {
        if (i + 1 != name.size() ||
            !std::isalpha(static_cast<unsigned char>(name[i])))
        {
            return false;
        }
    }
    return true;
}

/** Known FreeBSD 802.11 parent driver units (not wlan clones). Longest first. */
const char *const k_wifi_parent_prefixes[] = {
    "iwlwifi", /* LinuxKPI Intel */
    "rtw88", "rtw89",
    "urtwn", "upgt", "uath", "otus",
    "bwn", "bwi", "mwl", "malo", "rsu", "ral", "run", "rum",
    "ath", "iwn", "iwm", "iwx", "iwi",
    "rtw", "mtw", "zyd", "uwp",
    nullptr
};

bool name_is_safe_ifunit(const std::string& name)
{
    if (name.empty() || name.size() > 32)
    {
        return false;
    }
    for (unsigned char c : name)
    {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool is_wlan_clone_name(const std::string& name)
{
    return is_prefix_unit_local(name, "wlan");
}

bool is_wifi_parent_name(const std::string& name)
{
    if (name.empty() || is_wlan_clone_name(name))
    {
        return false;
    }
    for (int i = 0; k_wifi_parent_prefixes[i]; ++i)
    {
        if (is_prefix_unit_local(name, k_wifi_parent_prefixes[i]))
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> parse_wlan_devices_sysctl(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] () {
        if (cur.empty())
        {
            return;
        }
        /* Accept only plausible iface units */
        if (name_is_safe_ifunit(cur) &&
            (is_wifi_parent_name(cur) || is_wlan_clone_name(cur) ||
             /* allow unknown parent-style units from sysctl */
             (std::isalnum(static_cast<unsigned char>(cur.back())) &&
              cur.find_first_of(" \t\n\r") == std::string::npos)))
        {
            bool dup = false;
            for (const auto& e : out)
            {
                if (e == cur)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup)
            {
                out.push_back(cur);
            }
        }
        cur.clear();
    };
    for (char ch : text)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',' ||
            ch == ';')
        {
            flush();
        } else
        {
            cur.push_back(ch);
        }
    }
    flush();
    return out;
}

std::string next_wlan_clone_name(const std::vector<std::string>& existing_wlans)
{
    int used_max = -1;
    for (const auto& n : existing_wlans)
    {
        if (!is_wlan_clone_name(n) || n.size() < 5)
        {
            continue;
        }
        int num = 0;
        bool ok = true;
        for (size_t i = 4; i < n.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(n[i])))
            {
                ok = false;
                break;
            }
            num = num * 10 + (n[i] - '0');
        }
        if (ok && num > used_max)
        {
            used_max = num;
        }
    }
    return "wlan" + std::to_string(used_max + 1);
}

std::string build_wlan_create_command(const std::string& wlan_name,
    const std::string& parent_name)
{
    if (!is_wlan_clone_name(wlan_name) || !name_is_safe_ifunit(parent_name) ||
        parent_name.empty() || !name_is_safe_ifunit(wlan_name))
    {
        return {};
    }
    /* Prefer explicit unit so we know the name; FreeBSD accepts this form. */
    return "ifconfig " + wlan_name + " create wlandev " + parent_name;
}

std::vector<std::string> parents_needing_wlan_clone(
    const std::vector<std::string>& parents,
    const std::vector<std::string>& clone_parents)
{
    std::vector<std::string> need;
    for (const auto& p : parents)
    {
        if (p.empty())
        {
            continue;
        }
        bool has = false;
        for (const auto& c : clone_parents)
        {
            if (c == p)
            {
                has = true;
                break;
            }
        }
        if (!has)
        {
            need.push_back(p);
        }
    }
    return need;
}

std::string iface_driver_stem(const std::string& name)
{
    if (name.empty())
    {
        return {};
    }
    /* vm-public / vm-port* → stem "vm" (custom naming, not unit digits). */
    if (name.rfind("vm-", 0) == 0)
    {
        return "vm";
    }
    /* Strip trailing unit: letters/underscores, then digits, optional letter. */
    size_t i = 0;
    while (i < name.size() &&
        (std::isalpha(static_cast<unsigned char>(name[i])) || name[i] == '_'))
    {
        ++i;
    }
    if (i == 0 || i >= name.size())
    {
        /* No digit unit — whole name if alphanumeric-ish */
        return name_is_safe_ifunit(name) ? name : std::string{};
    }
    size_t stem_end = i;
    if (!std::isdigit(static_cast<unsigned char>(name[i])))
    {
        return {};
    }
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])))
    {
        ++i;
    }
    if (i < name.size())
    {
        /* epair0a: allow one trailing alpha */
        if (i + 1 == name.size() &&
            std::isalpha(static_cast<unsigned char>(name[i])))
        {
            /* ok */
        } else
        {
            return {};
        }
    }
    return name.substr(0, stem_end);
}

std::vector<std::string> parse_ifconfig_groups_field(const std::string& field)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] () {
        if (!cur.empty())
        {
            /* Drop FreeBSD viid-* annotations */
            if (cur.rfind("viid-", 0) != 0)
            {
                out.push_back(cur);
            }
            cur.clear();
        }
    };
    for (char ch : field)
    {
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == '\n' || ch == '\r')
        {
            flush();
        } else if (ch == '@')
        {
            /* viid-4c918@ → stop token */
            flush();
        } else
        {
            cur.push_back(ch);
        }
    }
    flush();
    return out;
}

InterfaceKind classify_from_media(const std::string& media)
{
    std::string m = media;
    while (!m.empty() && (m[0] == ' ' || m[0] == '\t'))
    {
        m.erase(m.begin());
    }
    if (m.rfind("IEEE 802.11", 0) == 0 || m.rfind("IEEE802.11", 0) == 0 ||
        m.find("802.11") != std::string::npos)
    {
        return InterfaceKind::Wireless;
    }
    if (m.rfind("Ethernet", 0) == 0 || m.rfind("ethernet", 0) == 0)
    {
        return InterfaceKind::Ethernet;
    }
    return InterfaceKind::Other;
}

InterfaceKind classify_from_groups(const std::vector<std::string>& groups)
{
    auto has = [&] (const char *g) {
        for (const auto& x : groups)
        {
            if (x == g)
            {
                return true;
            }
        }
        return false;
    };
    if (has("lo"))
    {
        return InterfaceKind::Loopback;
    }
    if (has("wlan"))
    {
        return InterfaceKind::Wireless;
    }
    if (has("bridge"))
    {
        return InterfaceKind::Bridge;
    }
    static const char *virt[] = {
        "tap", "tun", "epair", "gif", "gre", "lagg", "vlan", "wg", "vxlan",
        "stf", "disc", "pfsync", "pflog", nullptr
    };
    for (int i = 0; virt[i]; ++i)
    {
        if (has(virt[i]))
        {
            return InterfaceKind::Virtual;
        }
    }
    if (has("ether"))
    {
        return InterfaceKind::Ethernet;
    }
    return InterfaceKind::Other;
}

InterfaceKind classify_iface_name(const std::string& name)
{
    /* Name-only structural — no ethernet driver prefix table. */
    return classify_iface(name, {}, {}, false, {});
}

InterfaceKind classify_iface(const std::string& name,
    const std::string& media,
    const std::vector<std::string>& groups,
    bool ifa_loopback,
    const std::vector<std::string>& ethernet_stems)
{
    if (name.empty())
    {
        return InterfaceKind::Other;
    }

    if (ifa_loopback)
    {
        return InterfaceKind::Loopback;
    }

    /* Groups first (FreeBSD authoritative for family). */
    auto gk = classify_from_groups(groups);
    if (gk != InterfaceKind::Other)
    {
        return gk;
    }

    /* Structural wireless before media (parent radios often have no media). */
    if (is_wlan_clone_name(name) || is_wifi_parent_name(name))
    {
        return InterfaceKind::Wireless;
    }

    /* Media: Ethernet / 802.11 — after virtual groups so tap stays Virtual. */
    auto mk = classify_from_media(media);
    if (mk != InterfaceKind::Other)
    {
        return mk;
    }

    /* Name structural (non-ethernet). */
    if (name == "lo0" || (name.size() >= 2 && name[0] == 'l' && name[1] == 'o' &&
            (name.size() == 2 || std::isdigit(static_cast<unsigned char>(name[2])))))
    {
        return InterfaceKind::Loopback;
    }
    if (name.rfind("bastille", 0) == 0)
    {
        return InterfaceKind::Loopback;
    }
    if (name.rfind("bridge", 0) == 0 || name == "vm-public")
    {
        return InterfaceKind::Bridge;
    }
    if (name.rfind("tap", 0) == 0 || name.rfind("tun", 0) == 0 ||
        name.rfind("epair", 0) == 0 || name.rfind("vnet", 0) == 0 ||
        name.rfind("wg", 0) == 0 || name.rfind("gif", 0) == 0 ||
        name.rfind("gre", 0) == 0 || name.rfind("lagg", 0) == 0 ||
        name.rfind("vlan", 0) == 0 || name.rfind("vxlan", 0) == 0)
    {
        return InterfaceKind::Virtual;
    }

    /* Dynamically discovered ethernet driver stems (from earlier media probes). */
    if (!ethernet_stems.empty())
    {
        const std::string stem = iface_driver_stem(name);
        if (!stem.empty())
        {
            for (const auto& s : ethernet_stems)
            {
                if (s == stem)
                {
                    return InterfaceKind::Ethernet;
                }
            }
        }
    }

    return InterfaceKind::Other;
}

std::string format_wifi_connection_state(const InterfaceInfo& info)
{
    if (info.wifi_role == WifiRole::ParentRadio)
    {
        return info.wifi_needs_clone ? "No wlan interface" : "Radio";
    }
    if (info.kind != InterfaceKind::Wireless &&
        info.wifi_role != WifiRole::WlanClone)
    {
        return {};
    }
    if (!info.up)
    {
        return "Off";
    }

    /*
     * Priority: live wpa_state / ifconfig status first.
     * Do NOT treat a leftover SSID or DHCP lease as Connected while
     * wpa is SCANNING / DISCONNECTED (common after roam or rescan).
     */
    const std::string& st = info.wifi_wpa_state;
    const bool no_link = (info.status == "no carrier" || info.status == "down");

    if (st == "AUTHENTICATING" || st == "ASSOCIATING" || st == "4WAY_HANDSHAKE" ||
        st == "GROUP_HANDSHAKE")
    {
        return "Connecting";
    }
    if (st == "SCANNING")
    {
        return "Scanning";
    }
    if (st == "COMPLETED")
    {
        /* Fully authenticated; may still be waiting on DHCP */
        if (!info.wifi_ssid.empty() || !info.ipv4.empty() || !info.ipv6.empty())
        {
            return "Connected";
        }
        return "Associated";
    }
    if (st == "ASSOCIATED" || info.status == "associated")
    {
        if (!info.ipv4.empty() || !info.ipv6.empty())
        {
            return "Connected";
        }
        return "Associated";
    }
    if (st == "DISCONNECTED" || st == "INACTIVE" || st == "INTERFACE_DISABLED" ||
        no_link)
    {
        return "Disconnected";
    }
    /* No useful wpa state: fall back to ifconfig only */
    if (info.status == "associated" && !info.wifi_ssid.empty())
    {
        return (!info.ipv4.empty() || !info.ipv6.empty()) ? "Connected" : "Associated";
    }
    if (info.up)
    {
        return "On";
    }
    return "Off";
}

std::string format_address_summary(const InterfaceInfo& info, size_t max_addrs)
{
    /* IPv4 first, then IPv6 (caller already prefers global before link-local). */
    std::string s;
    size_t n = 0;
    auto add = [&] (const std::string& a) {
        if (a.empty() || n >= max_addrs)
        {
            return;
        }
        if (!s.empty())
        {
            s += '\n';
        }
        s += a;
        ++n;
    };
    for (const auto& a : info.ipv4)
    {
        add(a);
    }
    for (const auto& a : info.ipv6)
    {
        add(a);
    }
    return s;
}

std::string format_display_name(const InterfaceInfo& info)
{
    /* Compact panel label:
     *   aq0 · default
     *   99.48.162.238
     *   2600:1700:…
     * Wi‑Fi:
     *   wlan0 · CLOUDBSD · Connected
     */
    std::string base = info.name;
    if (info.wifi_role == WifiRole::ParentRadio)
    {
        if (info.wifi_needs_clone)
        {
            base += " · no wlan";
        } else
        {
            base += " · radio";
        }
    } else if (info.kind == InterfaceKind::Wireless ||
        info.wifi_role == WifiRole::WlanClone)
    {
        auto st = format_wifi_connection_state(info);
        if (!info.wifi_ssid.empty())
        {
            base += " · ";
            base += info.wifi_ssid;
        }
        if (!st.empty())
        {
            base += " · ";
            base += st;
        }
    } else if (info.is_default_route)
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

std::string format_media_speed(const std::string& media)
{
    if (media.empty())
    {
        return {};
    }
    /* Prefer parenthetical active media: (10Gbase-T <full-duplex>) */
    std::string s = media;
    const size_t lp = media.rfind('(');
    const size_t rp = media.rfind(')');
    if (lp != std::string::npos && rp != std::string::npos && rp > lp)
    {
        s = media.substr(lp + 1, rp - lp - 1);
    }

    auto has = [&] (const char *tok) {
        return s.find(tok) != std::string::npos;
    };
    /* Multi-gig first (order matters: 100G before 10G, 1000base before 100base) */
    if (has("400G") || has("400g"))
    {
        return "400 Gbps";
    }
    if (has("200G") || has("200g"))
    {
        return "200 Gbps";
    }
    if (has("100G") || has("100g"))
    {
        return "100 Gbps";
    }
    if (has("50G") || has("50g") || has("50Gbase"))
    {
        return "50 Gbps";
    }
    if (has("40G") || has("40g") || has("40Gbase"))
    {
        return "40 Gbps";
    }
    if (has("25G") || has("25g") || has("25Gbase"))
    {
        return "25 Gbps";
    }
    if (has("10G") || has("10g") || has("10Gbase") || has("10Gbase-T") ||
        has("10Gbase-SR") || has("10Gbase-LR"))
    {
        return "10 Gbps";
    }
    if (has("5Gbase") || has("5Gbase-T") || has("5000base"))
    {
        return "5 Gbps";
    }
    if (has("2.5G") || has("2500base") || has("2.5Gbase"))
    {
        return "2.5 Gbps";
    }
    if (has("1000base") || has("1000Base") || has("1Gbase") || has("Gigabit"))
    {
        return "1 Gbps";
    }
    if (has("100base") || has("100Base") || has("Fast Ethernet"))
    {
        return "100 Mbps";
    }
    if (has("10base") || has("10Base"))
    {
        return "10 Mbps";
    }
    return {};
}

std::string format_bitrate_kbps(unsigned max_bitrate_kbps)
{
    if (max_bitrate_kbps == 0)
    {
        return {};
    }
    /* NM / wpa MaxBitrate is Kb/s */
    if (max_bitrate_kbps >= 1000000)
    {
        /* ≥ 1 Gb/s — one decimal if not whole */
        const double gbps = max_bitrate_kbps / 1000000.0;
        if (gbps >= 10.0 || std::fabs(gbps - std::round(gbps)) < 0.05)
        {
            return std::to_string(static_cast<int>(std::lround(gbps))) + " Gbps";
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f Gbps", gbps);
        return buf;
    }
    if (max_bitrate_kbps >= 1000)
    {
        const unsigned mbps = (max_bitrate_kbps + 500) / 1000;
        return std::to_string(mbps) + " Mbps";
    }
    return std::to_string(max_bitrate_kbps) + " Kbps";
}

std::string format_iface_speed(const InterfaceInfo& info)
{
    if (info.link_speed_kbps > 0)
    {
        return format_bitrate_kbps(info.link_speed_kbps);
    }
    return format_media_speed(info.media);
}

namespace
{

std::string format_scaled(double value, const char *const *units, size_t n_units,
    const char *suffix)
{
    size_t u = 0;
    while (u + 1 < n_units && value >= 1000.0)
    {
        value /= 1000.0;
        ++u;
    }
    char buf[48];
    if (value >= 100.0 || u == 0)
    {
        std::snprintf(buf, sizeof(buf), "%.0f %s%s", value, units[u], suffix);
    }
    else if (value >= 10.0)
    {
        std::snprintf(buf, sizeof(buf), "%.1f %s%s", value, units[u], suffix);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.2f %s%s", value, units[u], suffix);
    }
    return buf;
}

} // namespace

std::string format_byte_count(uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    return format_scaled(static_cast<double>(bytes), units,
        sizeof(units) / sizeof(units[0]), "");
}

std::string format_byte_rate(uint64_t bytes_per_sec)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    return format_scaled(static_cast<double>(bytes_per_sec), units,
        sizeof(units) / sizeof(units[0]), "/s");
}

std::string format_bit_rate_from_bytes(uint64_t bytes_per_sec)
{
    /* Network link rates conventionally use decimal bits */
    const double bits = static_cast<double>(bytes_per_sec) * 8.0;
    static const char *units[] = {"bps", "Kbps", "Mbps", "Gbps", "Tbps"};
    size_t u = 0;
    double v = bits;
    while (u + 1 < sizeof(units) / sizeof(units[0]) && v >= 1000.0)
    {
        v /= 1000.0;
        ++u;
    }
    char buf[48];
    if (v >= 100.0 || u == 0)
    {
        std::snprintf(buf, sizeof(buf), "%.0f %s", v, units[u]);
    }
    else if (v >= 10.0)
    {
        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
    }
    return buf;
}

std::string icon_for_interface(const InterfaceInfo& info)
{
    bool active = info.up && info.running;
    switch (info.kind)
    {
        case InterfaceKind::Wireless:
        {
            /*
             * Adwaita: network-wireless-signal-{excellent,good,ok,weak,none}
             * + network-wireless-offline. Strength from live RSSI when known.
             */
            if (!active || info.status == "no carrier")
            {
                return "network-wireless-offline";
            }
            unsigned char pct = info.wifi_signal_pct;
            if (pct == 0 && info.wifi_signal_dbm != 0)
            {
                pct = wifi_signal_to_percent(info.wifi_signal_dbm);
            }
            if (pct == 0)
            {
                /* Associated but no sample yet — assume mid-good */
                return "network-wireless-signal-good";
            }
            return wifi_signal_icon_base(pct);
        }
        case InterfaceKind::Bridge:
            return active ? "network-server" : "network-offline";
        case InterfaceKind::Virtual:
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
    if (info.kind == InterfaceKind::Wireless && info.up && info.running)
    {
        unsigned char pct = info.wifi_signal_pct;
        if (pct == 0 && info.wifi_signal_dbm != 0)
        {
            pct = wifi_signal_to_percent(info.wifi_signal_dbm);
        }
        if (pct >= 80)
        {
            c.push_back("excellent");
        } else if (pct >= 55)
        {
            c.push_back("good");
        } else if (pct >= 30)
        {
            c.push_back("medium");
        } else if (pct >= 5)
        {
            c.push_back("weak");
        } else
        {
            c.push_back(info.status == "associated" ? "good" : "none");
        }
        if (info.is_default_route)
        {
            c.push_back("default");
        }
    } else if (info.up && info.running)
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

std::string format_wifi_radio_label(unsigned freq_mhz, unsigned max_bitrate_kbps,
    const std::string& generation_override)
{
    std::string band = format_wifi_band(freq_mhz);
    std::string gen  = !generation_override.empty() ?
        generation_override : format_wifi_generation(freq_mhz, max_bitrate_kbps);

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

std::string format_wifi_radio_label(const WifiScanEntry& entry)
{
    unsigned kbps = 0;
    if (entry.est_throughput_mbps > 0)
    {
        kbps = entry.est_throughput_mbps * 1000u;
    }
    return format_wifi_radio_label(entry.freq_mhz, kbps, entry.generation);
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

/**
 * Names that must never be offered as destroy targets.
 * No ethernet driver table — physical NICs are simply non-clone families
 * (is_destroyable returns false by default for unknown stems).
 */
bool is_permanent_hardware_name(const std::string& name)
{
    if (name == "lo0")
    {
        return true;
    }
    /* Parent 802.11 radios are permanent (destroy the wlan clone, not the driver). */
    if (is_wifi_parent_name(name))
    {
        return true;
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

std::string wifi_security_from_flags(const std::string& flags)
{
    const std::string f = flags;
    /* WEP before open check */
    if (f.find("WEP") != std::string::npos)
    {
        return "wep";
    }
    /* SAE-only (WPA3) */
    if (f.find("SAE") != std::string::npos && f.find("PSK") == std::string::npos)
    {
        return "sae";
    }
    if (f.find("WPA") != std::string::npos || f.find("PSK") != std::string::npos ||
        f.find("SAE") != std::string::npos)
    {
        return "wpa";
    }
    return "open";
}

unsigned char wifi_signal_to_percent(int signal_dbm)
{
    if (signal_dbm == 0)
    {
        return 0; /* unknown */
    }
    /* Rough map: -30 dBm ~ 100%, -90 dBm ~ 0% */
    if (signal_dbm >= -30)
    {
        return 100;
    }
    if (signal_dbm <= -90)
    {
        return 0;
    }
    int p = (signal_dbm + 90) * 100 / 60;
    if (p < 0)
    {
        p = 0;
    }
    if (p > 100)
    {
        p = 100;
    }
    return static_cast<unsigned char>(p);
}

unsigned char wifi_rssi_to_percent(double rssi)
{
    if (rssi <= 0.0)
    {
        return 0;
    }
    if (rssi >= 100.0)
    {
        return 100;
    }
    return static_cast<unsigned char>(rssi + 0.5);
}

std::string wifi_signal_icon_base(unsigned char percent)
{
    if (percent >= 80)
    {
        return "network-wireless-signal-excellent";
    }
    if (percent >= 55)
    {
        return "network-wireless-signal-good";
    }
    if (percent >= 30)
    {
        return "network-wireless-signal-ok";
    }
    if (percent >= 5)
    {
        return "network-wireless-signal-weak";
    }
    return "network-wireless-signal-none";
}

bool parse_ifconfig_list_sta_rssi(const std::string& text, double *rssi_out)
{
    if (!rssi_out)
    {
        return false;
    }
    std::istringstream iss(text);
    std::string line;
    bool header = true;
    while (std::getline(iss, line))
    {
        if (line.empty())
        {
            continue;
        }
        if (header)
        {
            /* First non-empty line is usually the column header (ADDR AID …) */
            if (line.find("ADDR") != std::string::npos ||
                line.find("RSSI") != std::string::npos)
            {
                header = false;
                continue;
            }
            header = false;
        }
        /* Station row: bssid … RATE RSSI IDLE …  e.g. "40M 63.0    0" */
        std::istringstream ls(line);
        std::string tok;
        std::vector<std::string> cols;
        while (ls >> tok)
        {
            cols.push_back(tok);
        }
        if (cols.size() < 5)
        {
            continue;
        }
        /* RSSI is typically column index 4 (0=ADDR 1=AID 2=CHAN 3=RATE 4=RSSI) */
        try
        {
            size_t idx = 4;
            if (cols[3].find('M') != std::string::npos ||
                cols[3].find('m') != std::string::npos)
            {
                idx = 4;
            }
            double v = std::stod(cols[idx]);
            if (v > 0.0 && v <= 127.0)
            {
                *rssi_out = v;
                return true;
            }
        } catch (...)
        {
            continue;
        }
    }
    return false;
}

bool parse_wpa_signal_level(const std::string& text, int *dbm_out)
{
    if (!dbm_out)
    {
        return false;
    }
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.rfind("level=", 0) == 0 || line.rfind("RSSI=", 0) == 0)
        {
            try
            {
                auto eq = line.find('=');
                int v = std::stoi(line.substr(eq + 1));
                /* dBm is negative; reject nonsense */
                if (v < 0 && v > -120)
                {
                    *dbm_out = v;
                    return true;
                }
            } catch (...)
            {
                return false;
            }
        }
    }
    return false;
}

WifiPhyCaps parse_wifi_ie_hex(const std::string& ie_hex)
{
    WifiPhyCaps caps;
    if (ie_hex.empty() || (ie_hex.size() % 2) != 0)
    {
        return caps;
    }
    std::vector<unsigned char> bytes;
    bytes.reserve(ie_hex.size() / 2);
    for (size_t i = 0; i + 1 < ie_hex.size(); i += 2)
    {
        auto nibble = [] (char c) -> int {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        };
        const int hi = nibble(ie_hex[i]);
        const int lo = nibble(ie_hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return WifiPhyCaps{}; /* corrupt hex */
        }
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }

    /*
     * Element IDs (IEEE 802.11):
     *   45  HT Capabilities
     *  191  VHT Capabilities
     *  255  Extension — first payload byte is Extension ID:
     *        35 HE Capabilities, 36 HE Operation
     *        45 HE 6 GHz Band Capabilities
     *       106 EHT Operation, 107 Multi-Link, 108 EHT Capabilities
     */
    size_t i = 0;
    while (i + 1 < bytes.size())
    {
        const unsigned id  = bytes[i];
        const unsigned len = bytes[i + 1];
        if (i + 2 + len > bytes.size())
        {
            break; /* truncated */
        }
        const unsigned char *payload = bytes.data() + i + 2;
        if (id == 45) /* HT Capabilities */
        {
            caps.ht = true;
        } else if (id == 191) /* VHT Capabilities */
        {
            caps.vht = true;
        } else if (id == 255 && len >= 1) /* Extension */
        {
            const unsigned eid = payload[0];
            if (eid == 35) /* HE Capabilities */
            {
                caps.he = true;
            } else if (eid == 36) /* HE Operation */
            {
                caps.he = true;
            } else if (eid == 45) /* HE 6 GHz Cap */
            {
                caps.he = true;
                caps.he_6ghz = true;
            } else if (eid == 108) /* EHT Capabilities */
            {
                caps.eht = true;
            } else if (eid == 106) /* EHT Operation */
            {
                caps.eht = true;
            } else if (eid == 107) /* Multi-Link */
            {
                caps.mlo = true;
                caps.eht = true; /* MLO implies EHT / Wi‑Fi 7 class */
            }
        }
        i += 2 + len;
    }
    return caps;
}

std::string wifi_generation_from_phy(const WifiPhyCaps& caps, unsigned freq_mhz)
{
    const bool band_6g = (freq_mhz >= 6000 && freq_mhz < 7000);
    if (caps.eht || caps.mlo)
    {
        return "Wi-Fi 7";
    }
    if (caps.he || caps.he_6ghz)
    {
        return (band_6g || caps.he_6ghz) ? "Wi-Fi 6E" : "Wi-Fi 6";
    }
    if (caps.vht)
    {
        return "Wi-Fi 5";
    }
    if (caps.ht)
    {
        return "Wi-Fi 4";
    }
    return {};
}

WpaBssDetail parse_wpa_bss_detail(const std::string& text)
{
    WpaBssDetail d;
    if (text.empty() || text.find("FAIL") == 0)
    {
        return d;
    }
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
        {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "bssid")
        {
            d.bssid = val;
        } else if (key == "ssid")
        {
            d.ssid = val;
        } else if (key == "flags")
        {
            d.flags = val;
        } else if (key == "ie")
        {
            d.ie_hex = val;
        } else if (key == "freq")
        {
            try
            {
                d.freq_mhz = static_cast<unsigned>(std::stoul(val));
            } catch (...)
            {
            }
        } else if (key == "level")
        {
            try
            {
                d.signal_dbm = std::stoi(val);
            } catch (...)
            {
            }
        } else if (key == "est_throughput")
        {
            try
            {
                d.est_throughput_mbps = static_cast<unsigned>(std::stoul(val));
            } catch (...)
            {
            }
        }
    }
    d.ok = !d.bssid.empty() || !d.ie_hex.empty() || !d.ssid.empty();
    return d;
}

void apply_wpa_bss_detail(WifiScanEntry& entry, const WpaBssDetail& d)
{
    if (!d.ok)
    {
        return;
    }
    if (entry.bssid.empty() && !d.bssid.empty())
    {
        entry.bssid = d.bssid;
    }
    if (entry.freq_mhz == 0 && d.freq_mhz > 0)
    {
        entry.freq_mhz = d.freq_mhz;
    }
    if (d.signal_dbm < 0)
    {
        entry.signal_dbm = d.signal_dbm;
    }
    if (!d.flags.empty())
    {
        entry.flags = d.flags;
        entry.security = wifi_security_from_flags(d.flags);
    }
    if (d.est_throughput_mbps > 0)
    {
        entry.est_throughput_mbps = d.est_throughput_mbps;
    }
    if (!d.ie_hex.empty())
    {
        entry.phy = parse_wifi_ie_hex(d.ie_hex);
        entry.generation = wifi_generation_from_phy(entry.phy,
            entry.freq_mhz ? entry.freq_mhz : d.freq_mhz);
    }
}

std::vector<WifiScanEntry> parse_wpa_scan_results(const std::string& text)
{
    std::vector<WifiScanEntry> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
        {
            continue;
        }
        /* Skip header */
        if (line.rfind("bssid", 0) == 0 || line.rfind("Selected interface", 0) == 0)
        {
            continue;
        }
        /* Fields are tab-separated; fall back to whitespace if needed. */
        std::vector<std::string> cols;
        if (line.find('\t') != std::string::npos)
        {
            std::string cur;
            for (char c : line)
            {
                if (c == '\t')
                {
                    cols.push_back(cur);
                    cur.clear();
                } else if (c != '\r')
                {
                    cur.push_back(c);
                }
            }
            cols.push_back(cur);
        } else
        {
            /* Rare: spaces — bssid, freq, signal, flags, ssid... */
            std::istringstream ls(line);
            std::string bssid, freq, sig, flags;
            if (!(ls >> bssid >> freq >> sig >> flags))
            {
                continue;
            }
            std::string ssid;
            std::getline(ls, ssid);
            while (!ssid.empty() && (ssid[0] == ' ' || ssid[0] == '\t'))
            {
                ssid.erase(ssid.begin());
            }
            cols = {bssid, freq, sig, flags, ssid};
        }
        if (cols.size() < 5)
        {
            continue;
        }
        WifiScanEntry e;
        e.bssid = cols[0];
        try
        {
            e.freq_mhz = static_cast<unsigned>(std::stoul(cols[1]));
            e.signal_dbm = std::stoi(cols[2]);
        } catch (...)
        {
            continue;
        }
        e.flags = cols[3];
        e.ssid.clear();
        for (size_t i = 4; i < cols.size(); ++i)
        {
            if (i > 4)
            {
                e.ssid.push_back('\t');
            }
            e.ssid += cols[i];
        }
        if (e.ssid.empty())
        {
            continue; /* hidden / empty SSID */
        }
        if (e.flags.find("[P2P]") != std::string::npos &&
            e.flags.find("[ESS]") == std::string::npos)
        {
            continue;
        }
        e.security = wifi_security_from_flags(e.flags);

        /* Best signal per SSID */
        bool replaced = false;
        for (auto& existing : out)
        {
            if (existing.ssid == e.ssid)
            {
                if (e.signal_dbm > existing.signal_dbm)
                {
                    existing = e;
                }
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(),
        [] (const WifiScanEntry& a, const WifiScanEntry& b) {
            return a.signal_dbm > b.signal_dbm;
        });
    return out;
}

ValidationResult validate_wifi_ssid(const std::string& ssid)
{
    if (ssid.empty())
    {
        return validation_fail("Network name is required");
    }
    if (ssid.size() > 32)
    {
        return validation_fail("SSID must be at most 32 characters");
    }
    return validation_ok();
}

ValidationResult validate_wifi_wpa_psk(const std::string& key)
{
    if (key.empty())
    {
        return validation_fail("Password is required");
    }
    if (key.size() == 64)
    {
        for (char c : key)
        {
            if (!is_hex_digit(c))
            {
                return validation_fail(
                    "WPA password must be 8–63 characters (or 64 hex digits)");
            }
        }
        return validation_ok();
    }
    if (key.size() < 8 || key.size() > 63)
    {
        return validation_fail(
            "WPA password must be 8–63 characters (or 64 hex digits)");
    }
    return validation_ok();
}

ValidationResult validate_wifi_wep_key(const std::string& key)
{
    if (key.empty())
    {
        return validation_fail("WEP key is required");
    }
    auto all_hex = [] (const std::string& s) {
        for (char c : s)
        {
            if (!is_hex_digit(c))
            {
                return false;
            }
        }
        return true;
    };
    if ((key.size() == 10 || key.size() == 26) && all_hex(key))
    {
        return validation_ok();
    }
    if (key.size() == 5 || key.size() == 13)
    {
        return validation_ok();
    }
    return validation_fail("WEP key: 5 or 13 characters, or 10/26 hex digits");
}

ValidationResult validate_wifi_credentials(const std::string& security,
    const std::string& key)
{
    if (security == "open" || security == "none")
    {
        return validation_ok();
    }
    if (security == "wpa" || security == "wpa2" || security == "wpa3" ||
        security == "wpa-psk" || security == "sae")
    {
        return validate_wifi_wpa_psk(key);
    }
    if (security == "wep")
    {
        return validate_wifi_wep_key(key);
    }
    return validation_fail("Unknown security type");
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
