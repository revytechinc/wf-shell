#pragma once

/**
 * NetworkBackend — abstract product (Factory Method).
 *
 *   FreeBSD  → ifconfig / getifaddrs / route (first-class FreeBSD service)
 *   Linux    → null here; NetworkManager D-Bus is Linux-only in manager.cpp
 *   Unknown  → Null backend
 *
 * Construction:
 *   auto b = NetworkBackendFactory::create();
 *   auto b = NetworkBackendFactory::builder()
 *                .poll_interval_ms(2000)
 *                .include_virtual(false)
 *                .build();
 */

#include <memory>
#include <sigc++/connection.h>
#include <map>
#include <string>

#include "network.hpp"
#include "network-types.hpp"

/**
 * Abstract network operations used by NetworkManager / panel.
 * Never throw into UI code.
 */
class NetworkBackend
{
  public:
    using SignalDeviceAdded   = sigc::signal<void (std::shared_ptr<Network>)>;
    using SignalDeviceRemoved = sigc::signal<void (std::shared_ptr<Network>)>;
    using SignalNmStart       = sigc::signal<void (void)>;
    using SignalNmStop        = sigc::signal<void (void)>;
    using SignalPrimary       = sigc::signal<void (std::shared_ptr<Network>)>;
    using DeviceMap           = std::map<std::string, std::shared_ptr<Network>>;

    virtual ~NetworkBackend() = default;

    virtual const char *platform_name() const = 0;

    /** Connect / start polling or D-Bus. */
    virtual void connect() = 0;

    /** Stop the backend. */
    virtual void disconnect() = 0;

    virtual DeviceMap& devices() = 0;

    /**
     * Preferred “default route” / primary device for the tray icon.
     * May return nullptr when nothing is up.
     */
    virtual std::shared_ptr<Network> primary_device() = 0;

    SignalDeviceAdded signal_device_added;
    SignalDeviceRemoved signal_device_removed;
    SignalNmStart signal_nm_start;
    SignalNmStop signal_nm_stop;
    /** Emitted when the primary/default interface changes (FreeBSD). */
    SignalPrimary signal_primary_changed;
};

/**
 * Builder for NetworkBackend (GoF Builder).
 */
class NetworkBackendBuilder
{
  public:
    NetworkBackendBuilder& poll_interval_ms(int ms);
    NetworkBackendBuilder& include_virtual(bool v);
    NetworkBackendBuilder& include_bridge(bool v);
    NetworkBackendBuilder& include_down(bool v);
    NetworkBackendBuilder& include_loopback(bool v);

    std::unique_ptr<NetworkBackend> build() const;

    int poll_interval_ms() const { return opts_.poll_interval_ms; }
    bool include_virtual() const { return opts_.include_virtual; }
    bool include_bridge() const { return opts_.include_bridge; }
    bool include_down() const { return opts_.include_down; }
    bool include_loopback() const { return opts_.include_loopback; }

    const wf_net::ProbeOptions& probe_options() const { return opts_; }

  private:
    wf_net::ProbeOptions opts_;
};

/**
 * Factory Method entry — OS selection confined here.
 */
class NetworkBackendFactory
{
  public:
    static std::unique_ptr<NetworkBackend> create();
    static NetworkBackendBuilder builder();
};

namespace wf_net
{
namespace detail
{
std::unique_ptr<NetworkBackend> create_freebsd_network_backend(const NetworkBackendBuilder& b);
std::unique_ptr<NetworkBackend> create_null_network_backend(const NetworkBackendBuilder& b);
/** Test override for factory platform string; nullptr restores host. */
void set_network_platform_override_for_test(const char *name);
}
}
