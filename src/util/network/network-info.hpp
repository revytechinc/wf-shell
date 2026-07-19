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

/**
 * Detect whether the current user can elevate for network mutations.
 * Order: already root → doas -n → sudo -n. Fail-soft → AdminPrivilege::None
 * (information-only mode). Never throws.
 *
 * Host doas/sudo policy is system config, not part of this repo.
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
