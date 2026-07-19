/*
 * FreeBSD network backend — Factory product.
 * Probes via getifaddrs + route + ifconfig (wf_net::probe_interfaces).
 * Fingerprint-driven: no device_added/altered spam when nothing changed.
 */

#include "network-backend.hpp"
#include "freebsd-network.hpp"
#include "network-info.hpp"
#include "network-log.hpp"
#include "null.hpp"

#include <memory>
#include <string>
#include <vector>

#include <glibmm.h>

namespace
{

class FreeBSDNetworkBackend final : public NetworkBackend
{
  public:
    explicit FreeBSDNetworkBackend(NetworkBackendBuilder b) : builder_(std::move(b))
    {
        all_devices_.emplace("/", std::make_shared<NullNetwork>());
    }

    const char *platform_name() const override
    {
        return "freebsd";
    }

    void connect() override
    {
        refresh_devices();
        int ms = builder_.poll_interval_ms();
        poll_timer_ = Glib::signal_timeout().connect(
            [this] () -> bool {
                refresh_devices();
                return true;
            },
            ms);
        signal_nm_start.emit();
    }

    void disconnect() override
    {
        if (poll_timer_.connected())
        {
            poll_timer_.disconnect();
        }
        signal_nm_stop.emit();
    }

    DeviceMap& devices() override
    {
        return all_devices_;
    }

    std::shared_ptr<Network> primary_device() override
    {
        if (primary_path_.empty())
        {
            return nullptr;
        }
        auto it = all_devices_.find(primary_path_);
        if (it == all_devices_.end())
        {
            return nullptr;
        }
        return it->second;
    }

  private:
    void refresh_devices()
    {
        auto list = wf_net::probe_interfaces(builder_.probe_options());
        std::map<std::string, bool> seen;

        for (auto& info : list)
        {
            seen[info.path] = true;
            auto it = all_devices_.find(info.path);
            if (it == all_devices_.end())
            {
                if (info.kind == wf_net::InterfaceKind::Wireless ||
                    info.wifi_role != wf_net::WifiRole::None)
                {
                    wf_net::net_event_info("iface.added",
                        wf_net::wifi_state_fields(info), info.name);
                }
                auto net = std::make_shared<FreeBSDNetwork>(std::move(info));
                all_devices_.emplace(net->get_path(), net);
                signal_device_added.emit(net);
            } else
            {
                auto fbsd = std::dynamic_pointer_cast<FreeBSDNetwork>(it->second);
                if (fbsd)
                {
                    const auto before = fbsd->info();
                    const bool changed = fbsd->update_info(std::move(info));
                    if (changed &&
                        (before.kind == wf_net::InterfaceKind::Wireless ||
                         fbsd->info().kind == wf_net::InterfaceKind::Wireless ||
                         before.wifi_role != wf_net::WifiRole::None ||
                         fbsd->info().wifi_role != wf_net::WifiRole::None))
                    {
                        auto fields = wf_net::wifi_state_fields(fbsd->info());
                        fields.push_back(wf_net::field_str("prev_conn_state",
                            wf_net::format_wifi_connection_state(before)));
                        fields.push_back(wf_net::field_str("prev_ssid", before.wifi_ssid));
                        fields.push_back(wf_net::field_str("prev_wpa_state",
                            before.wifi_wpa_state));
                        wf_net::net_event_info("wifi.state.changed", fields,
                            fbsd->info().name);
                    }
                }
            }
        }

        /* Remove vanished interfaces */
        std::vector<std::string> removed;
        for (const auto& [path, _] : all_devices_)
        {
            if (path == "/")
            {
                continue;
            }
            if (!seen.count(path))
            {
                removed.push_back(path);
            }
        }
        for (const auto& path : removed)
        {
            signal_device_removed.emit(all_devices_[path]);
            all_devices_.erase(path);
        }

        /* Primary / default route */
        std::string want = wf_net::pick_primary_path(
            /* rebuild infos from devices for pick — use probe list paths */
            [&] () {
                std::vector<wf_net::InterfaceInfo> infos;
                for (const auto& [path, dev] : all_devices_)
                {
                    if (path == "/")
                    {
                        continue;
                    }
                    auto f = std::dynamic_pointer_cast<FreeBSDNetwork>(dev);
                    if (f)
                    {
                        infos.push_back(f->info());
                    }
                }
                return infos;
            }());

        if (want != primary_path_)
        {
            auto prev = primary_path_;
            primary_path_ = want;
            auto prim = primary_device();
            wf_net::net_event_info("route.primary.changed", {
                wf_net::field_str("prev_path", prev),
                wf_net::field_str("path", primary_path_),
                wf_net::field_str("iface",
                    prim ? prim->get_interface() : std::string{}),
                wf_net::field_str("icon",
                    prim ? prim->get_icon_symbolic() : std::string{}),
            }, prim ? prim->get_interface() : std::string{});
            signal_primary_changed.emit(prim);
        } else if (!primary_path_.empty())
        {
            /* Same primary path — still refresh tray if signal/icon changed */
            auto prim = primary_device();
            if (prim)
            {
                /* network_altered already fired from update_info when fingerprint changes */
                (void)prim;
            }
        }
    }

    NetworkBackendBuilder builder_;
    DeviceMap all_devices_;
    sigc::connection poll_timer_;
    std::string primary_path_;
};

class NullNetworkBackend final : public NetworkBackend
{
  public:
    explicit NullNetworkBackend(NetworkBackendBuilder)
    {
        all_devices_.emplace("/", std::make_shared<NullNetwork>());
    }

    const char *platform_name() const override
    {
        return "unknown";
    }

    void connect() override
    {
        signal_nm_start.emit();
    }

    void disconnect() override
    {
        signal_nm_stop.emit();
    }

    DeviceMap& devices() override
    {
        return all_devices_;
    }

    std::shared_ptr<Network> primary_device() override
    {
        return nullptr;
    }

  private:
    DeviceMap all_devices_;
};

} // namespace

namespace wf_net
{
namespace detail
{

std::unique_ptr<NetworkBackend> create_freebsd_network_backend(const NetworkBackendBuilder& b)
{
    return std::make_unique<FreeBSDNetworkBackend>(b);
}

std::unique_ptr<NetworkBackend> create_null_network_backend(const NetworkBackendBuilder& b)
{
    return std::make_unique<NullNetworkBackend>(b);
}

} // namespace detail
} // namespace wf_net
