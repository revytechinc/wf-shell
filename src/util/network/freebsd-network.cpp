#include "freebsd-network.hpp"

#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include <glibmm.h>

FreeBSDNetwork::FreeBSDNetwork(wf_net::InterfaceInfo info) :
    Network(info.path, nullptr), info_(std::move(info))
{
    this->interface = info_.name;
    fingerprint_ = wf_net::interface_fingerprint(info_);
    last_state = is_active() ? NM_DEVICE_STATE_ACTIVATED : NM_DEVICE_STATE_DISCONNECTED;
}

bool FreeBSDNetwork::update_info(wf_net::InterfaceInfo info)
{
    std::string fp = wf_net::interface_fingerprint(info);
    if (fp == fingerprint_)
    {
        return false;
    }
    info_ = std::move(info);
    fingerprint_ = std::move(fp);
    this->interface = info_.name;
    last_state = is_active() ? NM_DEVICE_STATE_ACTIVATED : NM_DEVICE_STATE_DISCONNECTED;
    network_altered.emit();
    return true;
}

std::string FreeBSDNetwork::get_name()
{
    return wf_net::format_display_name(info_);
}

std::string FreeBSDNetwork::get_friendly_name()
{
    return std::string(wf_net::kind_label(info_.kind)) + " (" + info_.name + ")";
}

std::string FreeBSDNetwork::get_interface()
{
    return info_.name;
}

std::vector<std::string> FreeBSDNetwork::get_css_classes()
{
    return wf_net::css_for_interface(info_);
}

bool FreeBSDNetwork::is_active()
{
    return info_.up && info_.running;
}

std::string FreeBSDNetwork::get_icon_name()
{
    return wf_net::icon_for_interface(info_);
}

void FreeBSDNetwork::disconnect()
{
    /* Best-effort; may need root. Fail soft. */
    std::string cmd = "ifconfig " + info_.name + " down";
    (void)system(cmd.c_str());
    info_.up = false;
    info_.running = false;
    fingerprint_ = wf_net::interface_fingerprint(info_);
    network_altered.emit();
}

bool FreeBSDNetwork::can_toggle()
{
    if (geteuid() == 0)
    {
        return true;
    }
    /* Do not claim sudo/doas without a real check; tray stays honest. */
    return false;
}
