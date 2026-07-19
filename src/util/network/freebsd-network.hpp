#pragma once

#include "network.hpp"
#include "network-types.hpp"

/**
 * FreeBSDNetwork — Network product backed by a live InterfaceInfo snapshot.
 * Updated by FreeBSDNetworkBackend on each probe (fingerprint-driven).
 */
class FreeBSDNetwork : public Network
{
  public:
    FreeBSDNetwork(wf_net::InterfaceInfo info);

    std::string get_name() override;
    std::string get_icon_name() override;
    std::vector<std::string> get_css_classes() override;
    std::string get_friendly_name() override;
    std::string get_interface() override;
    bool is_active() override;
    void disconnect() override;
    bool can_toggle() override;

    /** Replace snapshot if fingerprint changed; returns true if UI should refresh. */
    bool update_info(wf_net::InterfaceInfo info);

    const wf_net::InterfaceInfo& info() const { return info_; }

  private:
    wf_net::InterfaceInfo info_;
    std::string fingerprint_;
};
