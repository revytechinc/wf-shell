#include "network-info.hpp"
#include "network-log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace wf_net
{

static InfoHooks g_hooks;

InfoHooks& info_hooks()
{
    return g_hooks;
}

void reset_info_hooks()
{
    g_hooks = InfoHooks{};
}

static std::string run_cmd_real(const std::string& cmd)
{
    /* No shell metacharacters expected from our fixed callers. */
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp)
    {
        return {};
    }
    std::string out;
    std::array<char, 512> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp))
    {
        out += buf.data();
    }
    pclose(fp);
    return out;
}

/** Run cmd; return true if exit status is 0. */
static bool run_cmd_ok(const std::string& cmd)
{
    if (g_hooks.run_cmd)
    {
        /* Tests: hook returns "0" / "ok" for success, empty/fail otherwise */
        std::string o = g_hooks.run_cmd(cmd);
        return o == "0" || o == "ok" || o.find("uid=0") != std::string::npos;
    }
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp)
    {
        return false;
    }
    std::array<char, 256> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp))
    {
        /* discard */
    }
    int st = pclose(fp);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static std::string run_cmd(const std::string& cmd)
{
    if (g_hooks.run_cmd)
    {
        return g_hooks.run_cmd(cmd);
    }
    return run_cmd_real(cmd);
}

static std::string trim_ws(std::string s)
{
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t'))
    {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
    {
        s.pop_back();
    }
    return s;
}

static std::string parse_route_field(const std::string& text, const char *key)
{
    std::istringstream iss(text);
    std::string line;
    const size_t klen = std::strlen(key);
    while (std::getline(iss, line))
    {
        auto p = line.find(key);
        if (p == std::string::npos)
        {
            continue;
        }
        return trim_ws(line.substr(p + klen));
    }
    return {};
}

std::string parse_route_get_interface(const std::string& text)
{
    return parse_route_field(text, "interface:");
}

std::string parse_route_get_gateway(const std::string& text)
{
    return parse_route_field(text, "gateway:");
}

static void push_unique(std::vector<std::string>& vec, const std::string& addr)
{
    if (addr.empty())
    {
        return;
    }
    for (const auto& e : vec)
    {
        if (e == addr)
        {
            return;
        }
    }
    vec.push_back(addr);
}

void parse_ifconfig_detail(const std::string& text, InterfaceInfo& info)
{
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        line = trim_ws(line);
        if (line.rfind("ether ", 0) == 0 && info.mac.empty())
        {
            info.mac = trim_ws(line.substr(6));
        }
        if (line.rfind("media:", 0) == 0)
        {
            info.media = trim_ws(line.substr(6));
        }
        if (line.rfind("status:", 0) == 0)
        {
            info.status = trim_ws(line.substr(7));
        }
        if (line.rfind("groups:", 0) == 0)
        {
            for (const auto& g : parse_ifconfig_groups_field(line.substr(7)))
            {
                push_unique(info.groups, g);
            }
        }
        /* ifconfig may report "bridge members:" — treat as bridge group hint */
        if (line.rfind("bridge ", 0) == 0 || line.rfind("member:", 0) == 0)
        {
            push_unique(info.groups, "bridge");
        }
        /* FreeBSD wlan(4): "ssid MyNet channel 36 (5180 MHz …) bssid …"
         * or ssid "" when not associated. */
        if (line.rfind("ssid ", 0) == 0)
        {
            std::string rest = trim_ws(line.substr(5));
            if (rest.size() >= 2 && rest.front() == '"')
            {
                auto endq = rest.find('"', 1);
                if (endq != std::string::npos)
                {
                    info.wifi_ssid = rest.substr(1, endq - 1);
                    rest = trim_ws(rest.substr(endq + 1));
                }
            } else
            {
                /* Unquoted SSID until " channel" / " bssid" / end */
                auto ch = rest.find(" channel");
                auto bs = rest.find(" bssid");
                size_t cut = rest.size();
                if (ch != std::string::npos)
                {
                    cut = ch;
                }
                if (bs != std::string::npos && bs < cut)
                {
                    cut = bs;
                }
                info.wifi_ssid = trim_ws(rest.substr(0, cut));
                rest = (cut < rest.size()) ? trim_ws(rest.substr(cut)) : std::string{};
            }
            auto chpos = rest.find("channel ");
            if (chpos != std::string::npos)
            {
                unsigned ch = 0;
                if (std::sscanf(rest.c_str() + chpos + 8, "%u", &ch) == 1)
                {
                    info.wifi_channel = ch;
                }
            }
        }
        /* Parent radio for a wlan clone (when ifconfig prints it). */
        if (line.rfind("wlandev ", 0) == 0)
        {
            info.wifi_parent = trim_ws(line.substr(8));
            auto sp = info.wifi_parent.find(' ');
            if (sp != std::string::npos)
            {
                info.wifi_parent = info.wifi_parent.substr(0, sp);
            }
        }
        /* FreeBSD wlan(4) detail: "parent interface: iwlwifi0" */
        if (line.rfind("parent interface:", 0) == 0)
        {
            info.wifi_parent = trim_ws(line.substr(17));
            auto sp = info.wifi_parent.find(' ');
            if (sp != std::string::npos)
            {
                info.wifi_parent = info.wifi_parent.substr(0, sp);
            }
        }
        /* bssid aa:bb:… on same line as ssid or alone */
        {
            auto bp = line.find("bssid ");
            if (bp != std::string::npos)
            {
                std::string b = trim_ws(line.substr(bp + 6));
                auto sp = b.find(' ');
                if (sp != std::string::npos)
                {
                    b = b.substr(0, sp);
                }
                if (!b.empty())
                {
                    info.wifi_bssid = b;
                }
            }
        }
        /* inet 99.48.162.238 netmask 0xffffff80 broadcast ... */
        if (line.rfind("inet ", 0) == 0)
        {
            std::istringstream ls(line);
            std::string tag, addr;
            ls >> tag >> addr;
            push_unique(info.ipv4, addr);
        }
        /* inet6 2600:... prefixlen 64 ...  or  inet6 fe80::%aq0 prefixlen 64 scopeid ... */
        if (line.rfind("inet6 ", 0) == 0)
        {
            std::istringstream ls(line);
            std::string tag, addr;
            ls >> tag >> addr;
            /* strip %iface scope suffix from presentation */
            auto pct = addr.find('%');
            if (pct != std::string::npos)
            {
                addr = addr.substr(0, pct);
            }
            if (addr.rfind("fe80:", 0) == 0 || addr.rfind("fe80::", 0) == 0)
            {
                push_unique(info.ipv6, addr + "%ll");
            } else
            {
                push_unique(info.ipv6, addr);
            }
        }
    }
}

std::string default_route_interface_v4()
{
    std::string out = run_cmd("route -n get default 2>/dev/null");
    return parse_route_get_interface(out);
}

std::string default_route_interface_v6()
{
    /* FreeBSD: IPv6 default */
    std::string out = run_cmd("route -n get -inet6 default 2>/dev/null");
    return parse_route_get_interface(out);
}

static std::string default_gateway_v4()
{
    return parse_route_get_gateway(run_cmd("route -n get default 2>/dev/null"));
}

static std::string default_gateway_v6()
{
    return parse_route_get_gateway(run_cmd("route -n get -inet6 default 2>/dev/null"));
}

static std::string addr_to_string(const struct sockaddr *sa)
{
    if (!sa)
    {
        return {};
    }
    char buf[INET6_ADDRSTRLEN] = {};
    if (sa->sa_family == AF_INET)
    {
        auto *in = reinterpret_cast<const struct sockaddr_in*>(sa);
        if (!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf)))
        {
            return {};
        }
        return buf;
    }
    if (sa->sa_family == AF_INET6)
    {
        auto *in6 = reinterpret_cast<const struct sockaddr_in6*>(sa);
        if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr))
        {
            /* Skip fe80:: for primary display unless nothing else */
            if (!inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf)))
            {
                return {};
            }
            return std::string(buf) + "%ll"; /* mark link-local */
        }
        if (!inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf)))
        {
            return {};
        }
        return buf;
    }
    return {};
}

/** Run elevated ifconfig-style command via passwordless elevators only. */
static bool run_elevated_nopass(const std::string& cmdline)
{
    if (cmdline.empty())
    {
        return false;
    }
    if (geteuid() == 0)
    {
        return run_cmd_ok(cmdline + " 2>/dev/null");
    }
    if (run_cmd_ok("doas -n " + cmdline + " 2>/dev/null"))
    {
        return true;
    }
    if (run_cmd_ok("sudo -n " + cmdline + " 2>/dev/null"))
    {
        return true;
    }
    return false;
}

std::vector<std::string> list_wlan_parent_devices()
{
    /* FreeBSD: space-separated parent radio units */
    std::string out = run_cmd("sysctl -n net.wlan.devices 2>/dev/null");
    return parse_wlan_devices_sysctl(out);
}

std::string create_wlan_for_parent(const std::string& parent_name,
    const std::vector<std::string>& existing_wlans)
{
    if (parent_name.empty())
    {
        return {};
    }
    std::vector<std::string> have = existing_wlans;
    if (have.empty())
    {
        /* Discover live wlan clones from ifconfig -l */
        std::string list = run_cmd("ifconfig -l 2>/dev/null");
        std::istringstream iss(list);
        std::string tok;
        while (iss >> tok)
        {
            if (is_wlan_clone_name(tok))
            {
                have.push_back(tok);
            }
        }
    }
    const std::string wlan = next_wlan_clone_name(have);
    const std::string cmd  = build_wlan_create_command(wlan, parent_name);
    if (cmd.empty())
    {
        return {};
    }
    if (!run_elevated_nopass(cmd))
    {
        return {};
    }
    /* Confirm it appeared */
    std::string check = run_cmd("ifconfig " + wlan + " 2>/dev/null");
    if (check.empty())
    {
        return {};
    }
    return wlan;
}

std::string default_wpa_supplicant_conf()
{
    /* ctrl_interface + update_config so panel/wpa_cli can save networks. */
    return
        "ctrl_interface=/var/run/wpa_supplicant\n"
        "ctrl_interface_group=wheel\n"
        "update_config=1\n"
        "eapol_version=1\n"
        "ap_scan=1\n"
        "fast_reauth=1\n";
}

static bool wpa_cli_ok(const std::string& wlan, const std::string& args)
{
    if (!is_wlan_clone_name(wlan))
    {
        return false;
    }
    return run_cmd_ok("wpa_cli -i " + wlan + " " + args + " 2>/dev/null");
}

static bool ensure_wpa_conf_file()
{
    /* Already present? */
    if (run_cmd_ok("test -s /etc/wpa_supplicant.conf"))
    {
        return true;
    }
    /* Write minimal conf via elevated tee (no interactive shell). */
    const std::string body = default_wpa_supplicant_conf();
    /* Escape for single-quoted printf is awkward; use base64-ish via heredoc free path:
     * printf %s | doas tee. Body has no single quotes. */
    std::string cmd = "printf '%s' '" + body + "' | tee /etc/wpa_supplicant.conf >/dev/null"
                      " && chmod 600 /etc/wpa_supplicant.conf";
    /* body has newlines — printf '%s' with embedded newlines in single quotes is OK in shell */
    return run_elevated_nopass("sh -c " + std::string("\"") +
        "printf '%s' '" + body + "' > /etc/wpa_supplicant.conf && chmod 600 /etc/wpa_supplicant.conf\"");
}

/* Simpler: write via doas sh -c with cat and echo lines */
static bool ensure_wpa_conf_file_v2()
{
    if (run_cmd_ok("test -s /etc/wpa_supplicant.conf"))
    {
        return true;
    }
    /* Line-by-line so we avoid quoting hell. */
    const char *lines[] = {
        "ctrl_interface=/var/run/wpa_supplicant",
        "ctrl_interface_group=wheel",
        "update_config=1",
        "eapol_version=1",
        "ap_scan=1",
        "fast_reauth=1",
        nullptr,
    };
    std::string script = "umask 077; : > /etc/wpa_supplicant.conf";
    for (int i = 0; lines[i]; ++i)
    {
        script += "; echo '";
        script += lines[i];
        script += "' >> /etc/wpa_supplicant.conf";
    }
    script += "; chmod 600 /etc/wpa_supplicant.conf";
    return run_elevated_nopass("sh -c '" + script + "'");
}

static bool ensure_wpa_running(const std::string& wlan)
{
    if (!is_wlan_clone_name(wlan))
    {
        return false;
    }
    /* Already talking? */
    if (wpa_cli_ok(wlan, "ping"))
    {
        return true;
    }
    if (!ensure_wpa_conf_file_v2())
    {
        /* Still try default path if conf exists read-only */
        if (!run_cmd_ok("test -r /etc/wpa_supplicant.conf"))
        {
            return false;
        }
    }
    /* Start daemon for this iface */
    std::string cmd = "wpa_supplicant -B -i " + wlan +
        " -c /etc/wpa_supplicant.conf -P /var/run/wpa_supplicant/" + wlan + ".pid";
    if (!run_elevated_nopass(cmd))
    {
        /* Alternate: system rc */
        (void)run_elevated_nopass("service wpa_supplicant onestart");
    }
    /* Give control socket a moment (caller must not be the UI thread). */
    for (int i = 0; i < 10; ++i)
    {
        if (wpa_cli_ok(wlan, "ping"))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return wpa_cli_ok(wlan, "ping");
}

static std::string resolve_wlan_for_power(const std::string& iface_or_parent)
{
    if (iface_or_parent.empty())
    {
        return {};
    }
    if (is_wlan_clone_name(iface_or_parent))
    {
        /* Confirm exists */
        if (!run_cmd("ifconfig " + iface_or_parent + " 2>/dev/null").empty())
        {
            return iface_or_parent;
        }
        return {};
    }
    /* Parent radio — find existing clone or create */
    auto parents = list_wlan_parent_devices();
    bool is_parent = is_wifi_parent_name(iface_or_parent);
    for (const auto& p : parents)
    {
        if (p == iface_or_parent)
        {
            is_parent = true;
            break;
        }
    }
    if (!is_parent)
    {
        /* Maybe a down ethernet-style wireless name — try as-is */
        if (!run_cmd("ifconfig " + iface_or_parent + " 2>/dev/null").empty())
        {
            return iface_or_parent;
        }
        return {};
    }

    /* Look for clone bound to this parent */
    std::string list = run_cmd("ifconfig -l 2>/dev/null");
    std::istringstream iss(list);
    std::string tok;
    std::vector<std::string> wlans;
    while (iss >> tok)
    {
        if (!is_wlan_clone_name(tok))
        {
            continue;
        }
        wlans.push_back(tok);
        std::string idx = tok.substr(4);
        std::string p = trim_ws(run_cmd(
            "sysctl -n net.wlan." + idx + ".%parent 2>/dev/null"));
        if (p.empty())
        {
            InterfaceInfo tmp;
            parse_ifconfig_detail(run_cmd("ifconfig " + tok + " 2>/dev/null"), tmp);
            p = tmp.wifi_parent;
        }
        if (p == iface_or_parent || (p.empty() && parents.size() == 1))
        {
            return tok;
        }
    }
    return create_wlan_for_parent(iface_or_parent, wlans);
}

static int wifi_dedupe_saved_networks(const std::string& wlan);

WifiPowerResult wifi_turn_on(const std::string& iface_or_parent)
{
    WifiPowerResult r;
    net_event_info("wifi.power.on.request", {
        field_str("target", iface_or_parent),
    }, iface_or_parent);
    if (iface_or_parent.empty())
    {
        r.detail = "no interface";
        net_event_error("wifi.power.on", {field_str("error", r.detail)}, iface_or_parent);
        return r;
    }
    if (probe_admin_privilege() == AdminPrivilege::None)
    {
        r.detail = "admin required";
        net_event_error("wifi.power.on", {field_str("error", r.detail)}, iface_or_parent);
        return r;
    }

    std::string wlan = resolve_wlan_for_power(iface_or_parent);
    if (wlan.empty())
    {
        r.detail = "could not create wlan interface";
        net_event_error("wifi.power.on", {field_str("error", r.detail)}, iface_or_parent);
        return r;
    }
    r.wlan = wlan;

    /* 1. Bring link up + allow IPv6 (IFDISABLED is common on fresh wlan) */
    if (!run_elevated_nopass("ifconfig " + wlan + " up"))
    {
        /* May already be up */
        std::string det = run_cmd("ifconfig " + wlan + " 2>/dev/null");
        if (det.find("UP") == std::string::npos && det.find("<UP") == std::string::npos)
        {
            r.detail = "ifconfig up failed";
            return r;
        }
    }
    (void)run_elevated_nopass("ifconfig " + wlan + " inet6 -ifdisabled");
    (void)run_elevated_nopass("ifconfig " + wlan + " inet6 accept_rtadv");

    /* 2. wpa_supplicant for association */
    bool wpa = ensure_wpa_running(wlan);
    if (wpa)
    {
        /* Collapse duplicate SSID blocks left by older always-add joins. */
        (void)wifi_dedupe_saved_networks(wlan);
        /* Enable all saved networks and poke reassociate (no-op if none). */
        (void)wpa_cli_ok(wlan, "reconfigure");
        (void)wpa_cli_ok(wlan, "enable_network all");
        (void)wpa_cli_ok(wlan, "reassociate");
        (void)wpa_cli_ok(wlan, "scan");
    }

    /* 3. DHCP — fire-and-forget only (never wait for a lease here). */
    InterfaceInfo cur;
    cur.name = wlan;
    parse_ifconfig_detail(run_cmd("ifconfig " + wlan + " 2>/dev/null"), cur);
    if (cur.ipv4.empty())
    {
        (void)run_elevated_nopass("dhclient -b " + wlan);
    }

    r.ok = true;
    if (wpa)
    {
        r.detail = "Wi-Fi on (" + wlan + ")";
    } else
    {
        r.detail = "Wi-Fi link up (" + wlan + "); wpa_supplicant not ready";
    }
    net_event_info("wifi.power.on", {
        field_bool("ok", r.ok),
        field_str("wlan", r.wlan),
        field_bool("wpa", wpa),
        field_str("detail", r.detail),
    }, r.wlan);
    return r;
}

WifiPowerResult wifi_turn_off(const std::string& iface_or_parent)
{
    WifiPowerResult r;
    net_event_info("wifi.power.off.request", {
        field_str("target", iface_or_parent),
    }, iface_or_parent);
    if (iface_or_parent.empty())
    {
        r.detail = "no interface";
        return r;
    }
    if (probe_admin_privilege() == AdminPrivilege::None)
    {
        r.detail = "admin required";
        return r;
    }

    std::string wlan = iface_or_parent;
    if (!is_wlan_clone_name(wlan))
    {
        wlan = resolve_wlan_for_power(iface_or_parent);
    }
    if (wlan.empty())
    {
        r.detail = "no wlan to turn off";
        return r;
    }
    r.wlan = wlan;

    (void)wpa_cli_ok(wlan, "disconnect");
    if (!run_elevated_nopass("ifconfig " + wlan + " down"))
    {
        r.detail = "ifconfig down failed";
        net_event_error("wifi.power.off", {
            field_str("wlan", wlan),
            field_str("error", r.detail),
        }, wlan);
        return r;
    }
    r.ok = true;
    r.detail = "Wi-Fi off (" + wlan + ")";
    net_event_info("wifi.power.off", {
        field_bool("ok", true),
        field_str("wlan", wlan),
        field_str("detail", r.detail),
    }, wlan);
    return r;
}

bool wifi_wpa_ready(const std::string& wlan)
{
    return wpa_cli_ok(wlan, "ping");
}

static std::string to_hex_bytes(const std::string& s)
{
    static const char *hexd = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s)
    {
        out.push_back(hexd[c >> 4]);
        out.push_back(hexd[c & 0xf]);
    }
    return out;
}

/** Shell-escape a wpa_cli quoted string value: '"text"' with safe outer single quotes. */
static std::string wpa_cli_shell_str(const std::string& text)
{
    /* Result is: '"content"'  so wpa_cli sees "content" */
    std::string inner;
    for (char c : text)
    {
        if (c == '\\' || c == '"')
        {
            inner.push_back('\\');
        }
        if (c == '\'')
        {
            /* break single-quote, insert \', resume */
            inner += "'\\''";
            continue;
        }
        inner.push_back(c);
    }
    return "'\"" + inner + "\"'";
}

static std::string wpa_cli_run(const std::string& wlan, const std::string& args)
{
    if (!is_wlan_clone_name(wlan))
    {
        return {};
    }
    return run_cmd("wpa_cli -i " + wlan + " " + args + " 2>/dev/null");
}

/**
 * Drop duplicate network blocks (same SSID) from the live wpa_supplicant
 * config and persist. Keeps CURRENT, else lowest id. Safe no-op if none.
 */
static int wifi_dedupe_saved_networks(const std::string& wlan)
{
    if (!is_wlan_clone_name(wlan) || !wpa_cli_ok(wlan, "ping"))
    {
        return 0;
    }
    auto listed = parse_wpa_list_networks(wpa_cli_run(wlan, "list_networks"));
    std::map<std::string, std::vector<WpaNetworkRow>> by_ssid;
    for (const auto& row : listed)
    {
        if (row.ssid.empty())
        {
            continue;
        }
        by_ssid[row.ssid].push_back(row);
    }
    int removed = 0;
    for (auto& it : by_ssid)
    {
        if (it.second.size() < 2)
        {
            continue;
        }
        std::vector<int> rem;
        (void)wpa_pick_network_id_for_ssid(it.second, it.first, &rem);
        for (int rid : rem)
        {
            if (wpa_cli_ok(wlan, "remove_network " + std::to_string(rid)))
            {
                ++removed;
                net_event_info("wifi.network.dedupe", {
                    field_str("ssid", it.first),
                    field_int("removed_id", rid),
                }, wlan);
            }
        }
    }
    if (removed > 0)
    {
        (void)wpa_cli_ok(wlan, "save_config");
        net_event_info("wifi.network.dedupe.done", {
            field_int("removed", removed),
        }, wlan);
    }
    return removed;
}

/**
 * Enrich scan rows with beacon IEs from `wpa_cli bss <bssid>`.
 * FreeBSD ifconfig list scan omits EHT; raw ie= is how we label Wi‑Fi 7.
 * Background-thread only (many wpa_cli calls).
 */
static void enrich_scan_with_bss_ies(const std::string& wlan,
    std::vector<WifiScanEntry>& aps)
{
    size_t eht_n = 0, he_n = 0;
    for (auto& e : aps)
    {
        if (e.bssid.empty())
        {
            continue;
        }
        /* wpa_cli accepts bssid with colons */
        auto detail = parse_wpa_bss_detail(wpa_cli_run(wlan, "bss " + e.bssid));
        apply_wpa_bss_detail(e, detail);
        if (e.phy.eht || e.phy.mlo)
        {
            ++eht_n;
        } else if (e.phy.he)
        {
            ++he_n;
        }
    }
    if (!aps.empty())
    {
        net_event_info("wifi.scan.phy", {
            field_int("aps", static_cast<long long>(aps.size())),
            field_int("wifi7", static_cast<long long>(eht_n)),
            field_int("wifi6", static_cast<long long>(he_n)),
        }, wlan);
    }
}

std::vector<WifiScanEntry> wifi_scan(const std::string& wlan, int wait_ms)
{
    std::vector<WifiScanEntry> empty;
    net_event_info("wifi.scan.start", {
        field_str("wlan", wlan),
        field_int("wait_ms", wait_ms),
    }, wlan);
    if (!is_wlan_clone_name(wlan))
    {
        net_event_warn("wifi.scan", {field_str("error", "not_wlan")}, wlan);
        return empty;
    }
    /* Ensure link up + wpa */
    (void)run_elevated_nopass("ifconfig " + wlan + " up");
    if (!ensure_wpa_running(wlan))
    {
        net_event_error("wifi.scan", {field_str("error", "wpa_not_ready")}, wlan);
        return empty;
    }
    (void)wpa_cli_ok(wlan, "scan");
    if (wait_ms < 500)
    {
        wait_ms = 500;
    }
    if (wait_ms > 15000)
    {
        wait_ms = 15000;
    }
    /*
     * Wait for scan results. MUST only be called from a background worker —
     * never the GTK main loop (each sleep would freeze the panel).
     */
    int left = wait_ms;
    std::vector<WifiScanEntry> final;
    while (left > 0)
    {
        int chunk = left > 400 ? 400 : left;
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
        left -= chunk;
        auto partial = parse_wpa_scan_results(wpa_cli_run(wlan, "scan_results"));
        /* Return as soon as we have APs after ~1.2s minimum settle */
        if (!partial.empty() && left < wait_ms - 1200)
        {
            final = std::move(partial);
            enrich_scan_with_bss_ies(wlan, final);
            net_event_info("wifi.scan.done", {
                field_int("count", static_cast<long long>(final.size())),
                field_str("first_ssid", final.empty() ? "" : final.front().ssid),
                field_str("first_gen", final.empty() ? "" : final.front().generation),
                field_bool("early", true),
            }, wlan);
            return final;
        }
    }
    final = parse_wpa_scan_results(wpa_cli_run(wlan, "scan_results"));
    enrich_scan_with_bss_ies(wlan, final);
    net_event_info("wifi.scan.done", {
        field_int("count", static_cast<long long>(final.size())),
        field_str("first_ssid", final.empty() ? "" : final.front().ssid),
        field_str("first_gen", final.empty() ? "" : final.front().generation),
        field_bool("early", false),
    }, wlan);
    return final;
}

WifiPowerResult wifi_join(const std::string& wlan, const std::string& ssid,
    const std::string& security, const std::string& key)
{
    WifiPowerResult r;
    r.wlan = wlan;
    net_event_info("wifi.join.request", {
        field_str("ssid", ssid),
        field_str("security", security),
        field_bool("has_key", !key.empty()),
    }, wlan);
    auto sv = validate_wifi_ssid(ssid);
    if (!sv.ok)
    {
        r.detail = sv.message;
        net_event_error("wifi.join", {field_str("error", r.detail)}, wlan);
        return r;
    }
    auto cv = validate_wifi_credentials(security, key);
    if (!cv.ok)
    {
        r.detail = cv.message;
        net_event_error("wifi.join", {field_str("error", r.detail)}, wlan);
        return r;
    }
    if (!is_wlan_clone_name(wlan))
    {
        r.detail = "not a wlan interface";
        net_event_error("wifi.join", {field_str("error", r.detail)}, wlan);
        return r;
    }
    (void)run_elevated_nopass("ifconfig " + wlan + " up");
    (void)run_elevated_nopass("ifconfig " + wlan + " inet6 -ifdisabled");
    (void)run_elevated_nopass("ifconfig " + wlan + " inet6 accept_rtadv");
    if (!ensure_wpa_running(wlan))
    {
        r.detail = "wpa_supplicant not ready";
        net_event_error("wifi.join", {field_str("error", r.detail)}, wlan);
        return r;
    }

    /*
     * Reuse an existing network block for this SSID (and drop duplicates).
     * Always add_network was creating CLOUDBSD/REVYNET clones on every join.
     */
    auto listed = parse_wpa_list_networks(wpa_cli_run(wlan, "list_networks"));
    std::vector<int> remove_ids;
    int keep_id = wpa_pick_network_id_for_ssid(listed, ssid, &remove_ids);
    for (int rid : remove_ids)
    {
        (void)wpa_cli_ok(wlan, "remove_network " + std::to_string(rid));
        net_event_info("wifi.network.dedupe", {
            field_str("ssid", ssid),
            field_int("removed_id", rid),
        }, wlan);
    }
    if (!remove_ids.empty())
    {
        (void)wpa_cli_ok(wlan, "save_config");
    }

    std::string net_id;
    bool reused = false;
    if (keep_id >= 0)
    {
        net_id = std::to_string(keep_id);
        reused = true;
        /* Clear disabled bit if prior join left the block off. */
        (void)wpa_cli_ok(wlan, "enable_network " + net_id);
    } else
    {
        std::string add_out = trim_ws(wpa_cli_run(wlan, "add_network"));
        if (add_out.empty() || add_out == "FAIL")
        {
            r.detail = "add_network failed";
            net_event_error("wifi.join", {field_str("error", r.detail)}, wlan);
            return r;
        }
        for (char c : add_out)
        {
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                net_id.push_back(c);
            } else if (!net_id.empty())
            {
                break;
            }
        }
        if (net_id.empty())
        {
            r.detail = "bad network id";
            return r;
        }
    }

    /* SSID as hex — no shell quoting issues */
    if (!wpa_cli_ok(wlan, "set_network " + net_id + " ssid " + to_hex_bytes(ssid)))
    {
        r.detail = "set ssid failed";
        return r;
    }

    std::string sec = security;
    if (sec == "wpa2" || sec == "wpa3" || sec == "wpa-psk")
    {
        sec = "wpa";
    }
    if (sec == "open" || sec == "none")
    {
        (void)wpa_cli_ok(wlan, "set_network " + net_id + " key_mgmt NONE");
    } else if (sec == "sae")
    {
        (void)wpa_cli_ok(wlan, "set_network " + net_id + " key_mgmt SAE");
        std::string cmd = "wpa_cli -i " + wlan + " set_network " + net_id +
            " psk " + wpa_cli_shell_str(key) + " 2>/dev/null";
        if (!run_cmd_ok(cmd) && !run_elevated_nopass(cmd))
        {
            r.detail = "set psk failed";
            return r;
        }
    } else if (sec == "wep")
    {
        (void)wpa_cli_ok(wlan, "set_network " + net_id + " key_mgmt NONE");
        (void)wpa_cli_ok(wlan, "set_network " + net_id + " wep_tx_keyidx 0");
        std::string cmd = "wpa_cli -i " + wlan + " set_network " + net_id +
            " wep_key0 " + wpa_cli_shell_str(key) + " 2>/dev/null";
        if (!run_cmd_ok(cmd) && !run_elevated_nopass(cmd))
        {
            r.detail = "set wep key failed";
            return r;
        }
    } else
    {
        /* WPA-PSK (covers WPA2 and mixed WPA2/SAE APs for most home nets) */
        (void)wpa_cli_ok(wlan, "set_network " + net_id + " key_mgmt WPA-PSK");
        std::string cmd;
        if (key.size() == 64)
        {
            cmd = "wpa_cli -i " + wlan + " set_network " + net_id + " psk " + key +
                " 2>/dev/null";
        } else
        {
            cmd = "wpa_cli -i " + wlan + " set_network " + net_id + " psk " +
                wpa_cli_shell_str(key) + " 2>/dev/null";
        }
        if (!run_cmd_ok(cmd) && !run_elevated_nopass(cmd))
        {
            r.detail = "set psk failed";
            return r;
        }
    }

    (void)wpa_cli_ok(wlan, "enable_network " + net_id);
    (void)wpa_cli_ok(wlan, "select_network " + net_id);
    (void)wpa_cli_ok(wlan, "save_config");
    (void)wpa_cli_ok(wlan, "reassociate");

    /* Brief wait for association (background thread only). */
    for (int i = 0; i < 12; ++i)
    {
        std::string st = wpa_cli_run(wlan, "status");
        if (st.find("wpa_state=COMPLETED") != std::string::npos ||
            st.find("wpa_state=ASSOCIATED") != std::string::npos)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }

    InterfaceInfo cur;
    cur.name = wlan;
    parse_ifconfig_detail(run_cmd("ifconfig " + wlan + " 2>/dev/null"), cur);
    if (cur.ipv4.empty())
    {
        /* Non-blocking DHCP: FreeBSD dhclient detaches; don't wait for lease. */
        (void)run_elevated_nopass("dhclient -b " + wlan);
        (void)run_elevated_nopass("dhclient " + wlan + " &");
    }

    r.ok = true;
    r.detail = reused ? ("Updated " + ssid) : ("Joined " + ssid);
    net_event_info("wifi.join", {
        field_bool("ok", true),
        field_bool("reused", reused),
        field_int("removed_dupes", static_cast<long long>(remove_ids.size())),
        field_str("ssid", ssid),
        field_str("security", security),
        field_str("detail", r.detail),
        field_str("net_id", net_id),
    }, wlan);
    return r;
}

int ensure_wlan_clones(const ProbeOptions& opts)
{
    if (!opts.auto_create_wlan)
    {
        return 0;
    }
    auto parents = list_wlan_parent_devices();
    if (parents.empty())
    {
        return 0;
    }

    /* Existing wlan clones + which parents they bind to */
    std::vector<std::string> wlans;
    std::vector<std::string> clone_parents;
    std::string list = run_cmd("ifconfig -l 2>/dev/null");
    {
        std::istringstream iss(list);
        std::string tok;
        while (iss >> tok)
        {
            if (is_wlan_clone_name(tok))
            {
                wlans.push_back(tok);
                /* Parent: sysctl net.wlan.N.%parent when available */
                std::string idx = tok.substr(4);
                std::string p = trim_ws(run_cmd(
                    "sysctl -n net.wlan." + idx + ".%parent 2>/dev/null"));
                if (p.empty())
                {
                    InterfaceInfo tmp;
                    tmp.name = tok;
                    parse_ifconfig_detail(
                        run_cmd("ifconfig " + tok + " 2>/dev/null"), tmp);
                    p = tmp.wifi_parent;
                }
                if (!p.empty())
                {
                    clone_parents.push_back(p);
                }
            }
        }
    }

    /* If we have any wlan but no parent mapping, treat first parent as covered
     * only when wlans.empty() is false and single parent — safer: only skip
     * parents we positively mapped. Unmapped wlans still leave parents "needing"
     * create; FreeBSD rejects second create for same parent with EEXIST-ish. */

    auto need = parents_needing_wlan_clone(parents, clone_parents);
    /* Heuristic: if there is at least one wlan and exactly one parent, assume covered */
    if (!wlans.empty() && parents.size() == 1 && need.size() == 1 &&
        need[0] == parents[0])
    {
        need.clear();
    }

    int created = 0;
    for (const auto& parent : need)
    {
        std::string w = create_wlan_for_parent(parent, wlans);
        if (!w.empty())
        {
            wlans.push_back(w);
            clone_parents.push_back(parent);
            ++created;
        }
    }
    return created;
}

static void apply_wifi_role(InterfaceInfo& info,
    const std::vector<std::string>& parents,
    const std::vector<std::string>& parents_with_clone)
{
    if (is_wlan_clone_name(info.name))
    {
        info.kind = InterfaceKind::Wireless;
        info.wifi_role = WifiRole::WlanClone;
        info.wifi_needs_clone = false;
        if (info.wifi_parent.empty())
        {
            /* Fall back: sole parent radio */
            if (parents.size() == 1)
            {
                info.wifi_parent = parents[0];
            }
        }
        return;
    }
    if (is_wifi_parent_name(info.name) ||
        std::find(parents.begin(), parents.end(), info.name) != parents.end())
    {
        info.kind = InterfaceKind::Wireless;
        info.wifi_role = WifiRole::ParentRadio;
        info.wifi_parent = info.name;
        info.wifi_needs_clone =
            std::find(parents_with_clone.begin(), parents_with_clone.end(),
                info.name) == parents_with_clone.end();
        if (info.media.empty())
        {
            info.media = "802.11 parent radio";
        }
        if (info.status.empty())
        {
            info.status = info.wifi_needs_clone ? "no wlan interface" : "parent";
        }
    }
}

std::vector<InterfaceInfo> probe_interfaces(const ProbeOptions& opts)
{
    /*
     * Do NOT auto-create wlan clones on the UI poll path — ensure_wlan_clones
     * runs elevated ifconfig and would freeze the panel. Creation happens in
     * wifi_turn_on() on a background worker when the user turns Wi‑Fi on.
     * (opts.auto_create_wlan retained for offline/tests that opt in explicitly.)
     */
    if (opts.auto_create_wlan)
    {
        static thread_local bool warned;
        (void)warned;
        /* Intentionally skipped on live panel path — see wifi_turn_on. */
    }

    std::vector<InterfaceInfo> result;
    const auto parents = list_wlan_parent_devices();

    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || !ifaddr)
    {
        /* Still surface parent radios from sysctl when stack listing fails. */
        if (opts.include_wifi_parents)
        {
            for (const auto& p : parents)
            {
                InterfaceInfo info;
                info.name = p;
                info.kind = InterfaceKind::Wireless;
                info.path = path_for_iface(p);
                info.wifi_role = WifiRole::ParentRadio;
                info.wifi_parent = p;
                info.wifi_needs_clone = true;
                info.media = "802.11 parent radio";
                info.status = "no wlan interface";
                result.push_back(std::move(info));
            }
        }
        return result;
    }

    std::map<std::string, InterfaceInfo> by_name;
    std::map<std::string, bool> loopback_flag;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_name)
        {
            continue;
        }
        std::string name = ifa->ifa_name;
        auto& info = by_name[name];
        if (info.name.empty())
        {
            info.name = name;
            info.path = path_for_iface(name);
        }

        unsigned flags = ifa->ifa_flags;
        info.up      = info.up || (flags & IFF_UP);
        info.running = info.running || (flags & IFF_RUNNING);
#ifdef IFF_LOOPBACK
        if (flags & IFF_LOOPBACK)
        {
            loopback_flag[name] = true;
        }
#endif
#ifdef IFF_LOWER_UP
        info.has_carrier = info.has_carrier || (flags & IFF_LOWER_UP);
#else
        info.has_carrier = info.running;
#endif

        if (!ifa->ifa_addr)
        {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            push_unique(info.ipv4, addr_to_string(ifa->ifa_addr));
        } else if (ifa->ifa_addr->sa_family == AF_INET6)
        {
            push_unique(info.ipv6, addr_to_string(ifa->ifa_addr));
        }
    }
    freeifaddrs(ifaddr);

    const std::string def4 = default_route_interface_v4();
    const std::string def6 = default_route_interface_v6();
    const std::string gw4  = default_gateway_v4();
    const std::string gw6  = default_gateway_v6();

    /* Pass 1: ifconfig detail (media, groups, ssid, parent). */
    std::vector<std::string> parents_with_clone;
    for (auto& [name, info] : by_name)
    {
        std::string detail = run_cmd("ifconfig " + name + " 2>/dev/null");
        if (!detail.empty())
        {
            parse_ifconfig_detail(detail, info);
            if (info.status == "active" || info.status == "associated")
            {
                info.running = true;
            }
        }
        if (is_wlan_clone_name(name))
        {
            if (info.wifi_parent.empty())
            {
                std::string idx = name.substr(4);
                info.wifi_parent = trim_ws(run_cmd(
                    "sysctl -n net.wlan." + idx + ".%parent 2>/dev/null"));
            }
            if (info.wifi_parent.empty() && parents.size() == 1)
            {
                info.wifi_parent = parents[0];
            }
            if (!info.wifi_parent.empty())
            {
                parents_with_clone.push_back(info.wifi_parent);
            }
            /*
             * wpa_cli status is authoritative for association.
             * ifconfig ssid is also authoritative when empty + no carrier.
             */
            const std::string if_ssid = info.wifi_ssid; /* from ifconfig parse */
            const std::string sock = "/var/run/wpa_supplicant/" + name;
            if (run_cmd_ok("test -S " + sock))
            {
                std::string st = run_cmd("wpa_cli -i " + name + " status 2>/dev/null");
                auto take = [&] (const char *key) -> std::string {
                    std::string k = std::string(key) + "=";
                    std::istringstream iss(st);
                    std::string line;
                    while (std::getline(iss, line))
                    {
                        if (line.rfind(k, 0) == 0)
                        {
                            return trim_ws(line.substr(k.size()));
                        }
                    }
                    return {};
                };
                const std::string wpa_st = take("wpa_state");
                const std::string wpa_ssid = take("ssid");
                const std::string wpa_bssid = take("bssid");
                const std::string wpa_ip = take("ip_address");
                info.wifi_wpa_state = wpa_st;

                const bool associated =
                    (wpa_st == "COMPLETED" || wpa_st == "ASSOCIATED");
                const bool mid_assoc =
                    (wpa_st == "AUTHENTICATING" || wpa_st == "ASSOCIATING" ||
                     wpa_st == "4WAY_HANDSHAKE" || wpa_st == "GROUP_HANDSHAKE");
                const bool lost =
                    (wpa_st == "SCANNING" || wpa_st == "DISCONNECTED" ||
                     wpa_st == "INACTIVE" || wpa_st == "INTERFACE_DISABLED");

                if (associated || mid_assoc)
                {
                    info.wifi_ssid = !wpa_ssid.empty() ? wpa_ssid : if_ssid;
                    if (!wpa_bssid.empty())
                    {
                        info.wifi_bssid = wpa_bssid;
                    }
                    info.running = true;
                    if (info.status == "no carrier" || info.status.empty())
                    {
                        info.status = "associated";
                    }
                } else if (lost || if_ssid.empty())
                {
                    /* Not associated — never keep a stale SSID label. */
                    info.wifi_ssid.clear();
                    info.wifi_bssid.clear();
                } else if (!if_ssid.empty() && info.status == "associated")
                {
                    info.wifi_ssid = if_ssid;
                }

                if (!wpa_ip.empty())
                {
                    push_unique(info.ipv4, wpa_ip);
                }

                /* Signal: prefer wpa bss level (dBm), else ifconfig list sta RSSI */
                if (associated || mid_assoc)
                {
                    int dbm = 0;
                    if (!info.wifi_bssid.empty())
                    {
                        std::string bss = run_cmd("wpa_cli -i " + name +
                            " bss " + info.wifi_bssid + " 2>/dev/null");
                        if (parse_wpa_signal_level(bss, &dbm))
                        {
                            info.wifi_signal_dbm = dbm;
                            info.wifi_signal_pct = wifi_signal_to_percent(dbm);
                        }
                    }
                    if (info.wifi_signal_pct == 0)
                    {
                        double rssi = 0;
                        std::string sta = run_cmd(
                            "ifconfig " + name + " list sta 2>/dev/null");
                        if (parse_ifconfig_list_sta_rssi(sta, &rssi))
                        {
                            info.wifi_signal_pct = wifi_rssi_to_percent(rssi);
                        }
                    }
                } else
                {
                    info.wifi_signal_dbm = 0;
                    info.wifi_signal_pct = 0;
                }
            } else
            {
                if (if_ssid.empty() &&
                    (info.status == "no carrier" || info.status.empty()))
                {
                    info.wifi_ssid.clear();
                    info.wifi_bssid.clear();
                }
                info.wifi_signal_dbm = 0;
                info.wifi_signal_pct = 0;
            }
        }
    }

    /*
     * Pass 2: dynamically discover ethernet driver stems from live media.
     * Only physical ethernet (media Ethernet) that is not a virtual/bridge/lo group.
     * Stems feed classify_iface for ifaces whose media line is briefly missing.
     */
    std::vector<std::string> ethernet_stems;
    for (const auto& [name, info] : by_name)
    {
        auto gk = classify_from_groups(info.groups);
        if (gk == InterfaceKind::Virtual || gk == InterfaceKind::Bridge ||
            gk == InterfaceKind::Loopback || gk == InterfaceKind::Wireless)
        {
            continue;
        }
        if (classify_from_media(info.media) != InterfaceKind::Ethernet)
        {
            continue;
        }
        std::string stem = iface_driver_stem(name);
        if (stem.empty() || stem == "tap" || stem == "tun" || stem == "epair" ||
            stem == "vlan" || stem == "lagg" || stem == "bridge")
        {
            continue;
        }
        bool seen = false;
        for (const auto& s : ethernet_stems)
        {
            if (s == stem)
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            ethernet_stems.push_back(stem);
        }
    }

    /* Pass 3: live classify + filter + wifi roles. */
    for (auto& [name, info] : by_name)
    {
        const bool is_lo = loopback_flag[name];
        info.kind = classify_iface(name, info.media, info.groups, is_lo, ethernet_stems);

        if (info.kind == InterfaceKind::Loopback && !opts.include_loopback)
        {
            continue;
        }
        if (info.kind == InterfaceKind::Virtual && !opts.include_virtual)
        {
            continue;
        }
        if (info.kind == InterfaceKind::Bridge && !opts.include_bridge)
        {
            continue;
        }
        if (!opts.include_down && !info.up)
        {
            continue;
        }

        info.is_default_route_v4 = (!def4.empty() && name == def4);
        info.is_default_route_v6 = (!def6.empty() && name == def6);
        info.is_default_route    = info.is_default_route_v4 || info.is_default_route_v6;
        if (info.is_default_route_v4)
        {
            info.gateway_v4 = gw4;
        }
        if (info.is_default_route_v6)
        {
            info.gateway_v6 = gw6;
        }

        apply_wifi_role(info, parents, parents_with_clone);

        /* Rate-limited probe event for wireless ifaces only (state fingerprint). */
        if (info.kind == InterfaceKind::Wireless ||
            info.wifi_role != WifiRole::None)
        {
            static thread_local std::map<std::string, std::string> last_fp;
            const std::string fp = interface_fingerprint(info) + "|" +
                format_wifi_connection_state(info);
            if (last_fp[name] != fp)
            {
                last_fp[name] = fp;
                net_event_info("wifi.probe", wifi_state_fields(info), name);
            }
        }

        /* Prefer global/ULA IPv6 first; keep link-local last (stripped of %ll mark) */
        std::vector<std::string> v6_global, v6_ll;
        for (const auto& a : info.ipv6)
        {
            if (a.find("%ll") != std::string::npos)
            {
                v6_ll.push_back(a.substr(0, a.find("%ll")));
            } else if (a.rfind("fe80:", 0) == 0 || a.rfind("fe80::", 0) == 0)
            {
                v6_ll.push_back(a);
            } else
            {
                v6_global.push_back(a);
            }
        }
        info.ipv6 = std::move(v6_global);
        for (auto& a : v6_ll)
        {
            push_unique(info.ipv6, a);
        }

        result.push_back(std::move(info));
    }

    /* Parent radios that never appear in getifaddrs (typical for iwlwifi0). */
    if (opts.include_wifi_parents)
    {
        for (const auto& p : parents)
        {
            bool present = false;
            for (const auto& r : result)
            {
                if (r.name == p)
                {
                    present = true;
                    break;
                }
            }
            if (present)
            {
                continue;
            }
            InterfaceInfo info;
            info.name = p;
            info.kind = InterfaceKind::Wireless;
            info.path = path_for_iface(p);
            info.wifi_role = WifiRole::ParentRadio;
            info.wifi_parent = p;
            info.wifi_needs_clone =
                std::find(parents_with_clone.begin(), parents_with_clone.end(), p) ==
                parents_with_clone.end();
            /* Once a clone exists, hide the parent by default (clone is the UI face).
             * Still list parent when no clone — otherwise Wi‑Fi is invisible. */
            if (!info.wifi_needs_clone)
            {
                continue;
            }
            info.media = "802.11 parent radio";
            info.status = "no wlan interface";
            info.up = false;
            info.running = false;
            result.push_back(std::move(info));
        }
    }

    /* Stable order: default first, wireless before others of same rank, then name */
    std::sort(result.begin(), result.end(), [] (const InterfaceInfo& a, const InterfaceInfo& b) {
        if (a.is_default_route != b.is_default_route)
        {
            return a.is_default_route > b.is_default_route;
        }
        auto rank = [] (const InterfaceInfo& i) {
            if (i.wifi_role == WifiRole::WlanClone)
            {
                return 0;
            }
            if (i.kind == InterfaceKind::Wireless)
            {
                return 1;
            }
            if (i.kind == InterfaceKind::Ethernet)
            {
                return 2;
            }
            return 3;
        };
        if (rank(a) != rank(b))
        {
            return rank(a) < rank(b);
        }
        return a.name < b.name;
    });

    return result;
}

AdminPrivilege probe_admin_privilege(std::string *method_out)
{
    if (method_out)
    {
        method_out->clear();
    }
    if (geteuid() == 0)
    {
        if (method_out)
        {
            *method_out = "root";
        }
        return AdminPrivilege::Root;
    }
    /* -n: non-interactive; succeeds if nopass or cached credentials */
    if (run_cmd_ok("doas -n true 2>/dev/null"))
    {
        if (method_out)
        {
            *method_out = "doas";
        }
        return AdminPrivilege::Doas;
    }
    if (run_cmd_ok("sudo -n true 2>/dev/null"))
    {
        if (method_out)
        {
            *method_out = "sudo";
        }
        return AdminPrivilege::Sudo;
    }
    /*
     * Elevator exists but password required: not information-only —
     * UI shows an auth dialog when the user starts a mutation.
     */
    if (run_cmd_ok("command -v doas >/dev/null 2>&1"))
    {
        if (method_out)
        {
            *method_out = "doas";
        }
        return AdminPrivilege::NeedsPassword;
    }
    if (run_cmd_ok("command -v sudo >/dev/null 2>&1"))
    {
        if (method_out)
        {
            *method_out = "sudo";
        }
        return AdminPrivilege::NeedsPassword;
    }
    return AdminPrivilege::None;
}

NetworkStackFeatures probe_features(const ProbeOptions& opts)
{
    NetworkStackFeatures f;
    auto list = probe_interfaces(opts);
    f.physical_ifaces = false;
    for (const auto& i : list)
    {
        if (i.kind == InterfaceKind::Ethernet || i.kind == InterfaceKind::Wireless)
        {
            f.physical_ifaces = true;
        }
        if (i.kind == InterfaceKind::Wireless)
        {
            f.wireless = true;
        }
        if (i.is_default_route)
        {
            f.default_route = true;
        }
    }
    /* wpa: presence of wpa_cli + any wlan — lightweight check */
    if (f.wireless && run_cmd_ok("command -v wpa_cli >/dev/null 2>&1"))
    {
        f.wpa = true;
    }
    std::string method;
    f.admin = probe_admin_privilege(&method);
    f.can_admin = (f.admin != AdminPrivilege::None);
    f.needs_password = (f.admin == AdminPrivilege::NeedsPassword);
    f.admin_method = method;
    return f;
}

static bool module_file_exists(const std::string& module_name)
{
    if (module_name.empty())
    {
        return false;
    }
    /* Fixed paths only — no user input in the path. */
    const std::string ko = module_name + ".ko";
    const std::string paths[] = {
        "/boot/kernel/" + ko,
        "/boot/modules/" + ko,
    };
    for (const auto& p : paths)
    {
        if (access(p.c_str(), R_OK) == 0)
        {
            return true;
        }
    }
    return false;
}

CreatePreflight probe_create_preflight(const std::string& type)
{
    const bool has_admin = (probe_admin_privilege() != AdminPrivilege::None);
    std::string c_out = run_cmd("ifconfig -C 2>/dev/null");
    auto catalog = parse_ifconfig_clone_list(c_out);
    const CloneTypeInfo *spec = find_clone_type(type);
    bool loaded = false;
    bool file_ok = false;
    if (spec && spec->module)
    {
        std::string kld = run_cmd("kldstat 2>/dev/null");
        loaded = kldstat_has_module(kld, spec->module);
        file_ok = module_file_exists(spec->module);
    }
    return evaluate_create_preflight(type, catalog, loaded, file_ok, has_admin);
}

std::vector<CreatePreflight> probe_create_catalog()
{
    std::vector<CreatePreflight> out;
    size_t n = 0;
    const CloneTypeInfo *types = known_clone_types(&n);
    for (size_t i = 0; i < n; ++i)
    {
        out.push_back(probe_create_preflight(types[i].type));
    }
    return out;
}

std::string pick_primary_path(const std::vector<InterfaceInfo>& list)
{
    /*
     * Tray icon follows the **live default route** only when that iface is up.
     * Wi‑Fi connected on a secondary path must NOT steal the icon.
     * If the default-route iface is down (e.g. aq0 admin-down), fall through
     * so a still-up Wi‑Fi path can become primary.
     */
    for (const auto& i : list)
    {
        if (i.is_default_route_v4 && i.is_default_route_v6 && i.up && i.running)
        {
            return i.path;
        }
    }
    for (const auto& i : list)
    {
        if (i.is_default_route_v4 && i.up && i.running)
        {
            return i.path;
        }
    }
    for (const auto& i : list)
    {
        if (i.is_default_route_v6 && i.up && i.running)
        {
            return i.path;
        }
    }
    /* Default-route iface missing/down: prefer any up Wireless with association */
    for (const auto& i : list)
    {
        if (i.up && i.running && i.kind == InterfaceKind::Wireless &&
            (i.status == "associated" || i.wifi_wpa_state == "COMPLETED" ||
             !i.wifi_ssid.empty()))
        {
            return i.path;
        }
    }
    for (const auto& i : list)
    {
        if (i.up && i.running && i.kind != InterfaceKind::Loopback &&
            i.kind != InterfaceKind::Bridge && i.kind != InterfaceKind::Virtual)
        {
            return i.path;
        }
    }
    for (const auto& i : list)
    {
        if (i.up && i.running)
        {
            return i.path;
        }
    }
    return {};
}

} // namespace wf_net
