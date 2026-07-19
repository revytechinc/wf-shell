/*
 * NetworkBackendBuilder + NetworkBackendFactory (GoF).
 *
 * FreeBSD → FreeBSDNetworkBackend (ifconfig/route probe) — sole network product.
 * Linux   → null poll backend; NetworkManager D-Bus is wired only in manager.cpp.
 * Other   → null (quiet degrade).
 */

#include "network-backend.hpp"
#include "platform.hpp"

#include <string>
#include <utility>

namespace
{
const char *g_platform_override = nullptr;
}

namespace wf_net
{
namespace detail
{

void set_network_platform_override_for_test(const char *name)
{
    g_platform_override = name;
}

static const char *effective_platform()
{
    if (g_platform_override)
    {
        return g_platform_override;
    }
    return wf_platform_name();
}

} // namespace detail
} // namespace wf_net

/* --- Builder --- */

NetworkBackendBuilder& NetworkBackendBuilder::poll_interval_ms(int ms)
{
    opts_.poll_interval_ms = ms > 0 ? ms : 3000;
    return *this;
}

NetworkBackendBuilder& NetworkBackendBuilder::include_virtual(bool v)
{
    opts_.include_virtual = v;
    return *this;
}

NetworkBackendBuilder& NetworkBackendBuilder::include_bridge(bool v)
{
    opts_.include_bridge = v;
    return *this;
}

NetworkBackendBuilder& NetworkBackendBuilder::include_down(bool v)
{
    opts_.include_down = v;
    return *this;
}

NetworkBackendBuilder& NetworkBackendBuilder::include_loopback(bool v)
{
    opts_.include_loopback = v;
    return *this;
}

std::unique_ptr<NetworkBackend> NetworkBackendBuilder::build() const
{
    const char *plat = wf_net::detail::effective_platform();
    if (plat && std::string(plat) == "freebsd")
    {
        return wf_net::detail::create_freebsd_network_backend(*this);
    }
    /* Linux path: manager uses D-Bus directly; null here means "no poll backend".
     * OpenBSD/NetBSD/unknown: null backend (quiet). */
    return wf_net::detail::create_null_network_backend(*this);
}

/* --- Factory --- */

std::unique_ptr<NetworkBackend> NetworkBackendFactory::create()
{
    return NetworkBackendBuilder{}.build();
}

NetworkBackendBuilder NetworkBackendFactory::builder()
{
    return NetworkBackendBuilder{};
}
