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

} // namespace wf_net
