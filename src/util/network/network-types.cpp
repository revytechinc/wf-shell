#include "network-types.hpp"

#include <cctype>

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

} // namespace wf_net
