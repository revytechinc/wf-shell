#include "network.hpp"

#include "gtkmm/gestureclick.h"
#include "gtkmm/gesturelongpress.h"
#include "network/network.hpp"
#include "platform.hpp"
#include "wf-popover.hpp"

#include <cstring>
#include <glibmm/spawn.h>
#include <gtk-utils.hpp>
#include <memory>
#include <string>
#include <vector>

WayfireNetworkInfo::WayfireNetworkInfo()
{}

void WayfireNetworkInfo::init(Gtk::Box *container)
{
    network_manager = NetworkManager::getInstance();
    button = std::make_unique<WayfireMenuWidget>("panel", "network");
    button->add_css_class("network");

    container->append(*button);
    button->append(icon);
    button->set_popup_child(control);
    button->open_on(1);

    icon.set_valign(Gtk::Align::CENTER);
    icon.add_css_class("widget-icon");

    /* Right-click / long-press: optional external command */
    auto click = Gtk::GestureClick::create();
    click->set_button(3);
    signals.push_back(click->signal_released().connect(
        [this] (int, double, double) { on_click(); }));
    signals.push_back(click->signal_pressed().connect(
        [click] (int, double, double) {
            click->set_state(Gtk::EventSequenceState::CLAIMED);
        }));

    auto touch = Gtk::GestureLongPress::create();
    touch->set_touch_only(true);
    signals.push_back(touch->signal_pressed().connect(
        [this] (double, double) { on_click(); }));

    button->add_controller(touch);
    button->add_controller(click);

    signals.push_back(network_manager->signal_default_changed().connect(
        sigc::mem_fun(*this, &WayfireNetworkInfo::set_connection)));
    set_connection(network_manager->get_primary_network());

    (void)no_label; /* ini key retained; tray never shows a text label */
}

void WayfireNetworkInfo::apply_activity_classes(const std::vector<std::string>& classes)
{
    static const char *keep[] = {"network", "widget-icon", nullptr};

    auto strip = [&] (Gtk::Widget& w) {
        for (const auto& c : w.get_css_classes())
        {
            bool retain = false;
            for (int i = 0; keep[i]; i++)
            {
                if (c == keep[i])
                {
                    retain = true;
                    break;
                }
            }
            if (!retain)
            {
                w.remove_css_class(c);
            }
        }
    };

    strip(*button);
    strip(icon);
    for (const auto& c : classes)
    {
        button->add_css_class(c);
        icon.add_css_class(c);
    }
}

void WayfireNetworkInfo::set_connection(std::shared_ptr<Network> network)
{
    if (!network)
    {
        apply_activity_classes({"none"});
        icon.set_from_icon_name("network-offline-symbolic");
        button->set_tooltip_text("No connection");
        return;
    }

    apply_activity_classes(network->get_css_classes());
    icon.set_from_icon_name(network->get_icon_symbolic());
    /* Detail for hover only — never drawn on the bar */
    button->set_tooltip_text(network->get_name());
}

void WayfireNetworkInfo::on_click()
{
    if (click_command_opt.value() != "default")
    {
        Glib::spawn_command_line_async(click_command_opt.value());
        return;
    }
    /* FreeBSD: left-click already opens the popover; no GNOME control center. */
    if (std::strcmp(wf_platform_name(), "freebsd") == 0)
    {
        return;
    }
    Glib::spawn_command_line_async("env XDG_CURRENT_DESKTOP=GNOME gnome-control-center");
}

WayfireNetworkInfo::~WayfireNetworkInfo()
{
    for (auto& signal : signals)
    {
        signal.disconnect();
    }
}
