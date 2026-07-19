#include "network.hpp"

#include "gtkmm/gestureclick.h"
#include "gtkmm/gesturelongpress.h"
#include "network/network.hpp"
#include "platform.hpp"
#include "wf-popover.hpp"

#include <cstring>
#include <glibmm/spawn.h>
#include <giomm/simpleactiongroup.h>
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
    button->get_scroll().set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    button->open_on(1);

    /* FreeBSD Wi‑Fi scan runs only while the popover is open. */
    signals.push_back(button->signal_popup().connect(
        [this] () { control.on_popover_open(); }));
    signals.push_back(button->signal_popdown().connect(
        [this] () { control.on_popover_close(); }));

    icon.set_valign(Gtk::Align::CENTER);
    icon.add_css_class("widget-icon");

    // Parent the right-click popover menu to the panel button
    gtk_widget_set_parent(GTK_WIDGET(popover_menu.gobj()), GTK_WIDGET(button->gobj()));

    // Create action group for the context menu actions
    auto actions = Gio::SimpleActionGroup::create();

    auto toggle_wifi_action = Gio::SimpleAction::create("toggle_wifi");
    signals.push_back(toggle_wifi_action->signal_activate().connect([this] (Glib::VariantBase) {
        auto primary = network_manager->get_primary_network();
        if (primary && primary->can_toggle()) {
            primary->toggle();
        }
    }));

    auto settings_action = Gio::SimpleAction::create("settings");
    signals.push_back(settings_action->signal_activate().connect([this] (Glib::VariantBase) {
        button->popup();
    }));

    auto editor_action = Gio::SimpleAction::create("editor");
    signals.push_back(editor_action->signal_activate().connect([this] (Glib::VariantBase) {
        run_connection_editor();
    }));

    actions->add_action(toggle_wifi_action);
    actions->add_action(settings_action);
    actions->add_action(editor_action);
    popover_menu.insert_action_group("networkaction", actions);

    /* Right-click: open custom context menu */
    auto click = Gtk::GestureClick::create();
    click->set_button(3);
    signals.push_back(click->signal_released().connect(
        [this] (int, double, double) { show_right_click_menu(); }));
    signals.push_back(click->signal_pressed().connect(
        [click] (int, double, double) {
            click->set_state(Gtk::EventSequenceState::CLAIMED);
        }));

    auto touch = Gtk::GestureLongPress::create();
    touch->set_touch_only(true);
    signals.push_back(touch->signal_pressed().connect(
        [this] (double, double) { show_right_click_menu(); }));

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
    run_connection_editor();
}

void WayfireNetworkInfo::run_connection_editor()
{
    if (click_command_opt.value() != "default")
    {
        Glib::spawn_command_line_async(click_command_opt.value());
        return;
    }

    if (std::strcmp(wf_platform_name(), "freebsd") == 0)
    {
        try {
            Glib::spawn_command_line_async("wpa_gui");
        } catch (...) {
            try {
                Glib::spawn_command_line_async("alice-terminal -e nmtui");
            } catch (...) {
                try {
                    Glib::spawn_command_line_async("foot -e nmtui");
                } catch (...) {
                    try {
                        Glib::spawn_command_line_async("alacritty -e nmtui");
                    } catch (...) {}
                }
            }
        }
        return;
    }

    try {
        Glib::spawn_command_line_async("nm-connection-editor");
    } catch (...) {
        try {
            Glib::spawn_command_line_async("env XDG_CURRENT_DESKTOP=GNOME gnome-control-center");
        } catch (...) {}
    }
}

void WayfireNetworkInfo::show_right_click_menu()
{
    auto menu = Gio::Menu::create();

    bool has_wifi = false;
    auto primary = network_manager->get_primary_network();
    if (primary)
    {
        for (const auto& c : primary->get_css_classes()) {
            if (c == "wifi") {
                has_wifi = true;
                break;
            }
        }
    }

    if (has_wifi) {
        auto wifi_item = Gio::MenuItem::create(primary->is_active() ? "Disconnect Wi-Fi" : "Connect Wi-Fi", "networkaction.toggle_wifi");
        menu->append_item(wifi_item);
    }

    auto settings_item = Gio::MenuItem::create("Network Settings...", "networkaction.settings");
    auto editor_item = Gio::MenuItem::create("Connection Editor...", "networkaction.editor");

    menu->append_item(settings_item);
    menu->append_item(editor_item);

    popover_menu.set_menu_model(menu);
    popover_menu.popup();
}

WayfireNetworkInfo::~WayfireNetworkInfo()
{
    for (auto& signal : signals)
    {
        signal.disconnect();
    }
    gtk_widget_unparent(GTK_WIDGET(popover_menu.gobj()));
}
