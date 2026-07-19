#include "network-info.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

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

std::vector<InterfaceInfo> probe_interfaces(const ProbeOptions& opts)
{
    std::vector<InterfaceInfo> result;
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || !ifaddr)
    {
        return result;
    }

    std::map<std::string, InterfaceInfo> by_name;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_name)
        {
            continue;
        }
        std::string name = ifa->ifa_name;
        auto kind = classify_iface_name(name);
        if (kind == InterfaceKind::Loopback && !opts.include_loopback)
        {
            continue;
        }
        if (kind == InterfaceKind::Virtual && !opts.include_virtual)
        {
            continue;
        }
        if (kind == InterfaceKind::Bridge && !opts.include_bridge)
        {
            continue;
        }

        auto& info = by_name[name];
        if (info.name.empty())
        {
            info.name = name;
            info.kind = kind;
            info.path = path_for_iface(name);
        }

        unsigned flags = ifa->ifa_flags;
        info.up      = info.up || (flags & IFF_UP);
        info.running = info.running || (flags & IFF_RUNNING);
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

    for (auto& [name, info] : by_name)
    {
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

        /* Media/status + fill any missing inet/inet6 from ifconfig */
        std::string detail = run_cmd("ifconfig " + name + " 2>/dev/null");
        if (!detail.empty())
        {
            parse_ifconfig_detail(detail, info);
            if (info.status == "active")
            {
                info.running = true;
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

    /* Stable order: default first, then name */
    std::sort(result.begin(), result.end(), [] (const InterfaceInfo& a, const InterfaceInfo& b) {
        if (a.is_default_route != b.is_default_route)
        {
            return a.is_default_route > b.is_default_route;
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
    /* -n: non-interactive; fails if a password would be required */
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
    /* Prefer dual-stack default, then v4 default, then v6-only default */
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
