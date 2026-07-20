#include <gtk-utils.hpp>
#include <iostream>

#include "battery.hpp"
#define POWER_PROFILE_PATH "/org/freedesktop/UPower/PowerProfiles"
#define POWER_PROFILE_NAME "org.freedesktop.UPower.PowerProfiles"
#define UPOWER_NAME "org.freedesktop.UPower"
#define DISPLAY_DEVICE "/org/freedesktop/UPower/devices/DisplayDevice"

#define ICON           "IconName"
#define TYPE           "Type"
#define STATE          "State"
#define PERCENTAGE     "Percentage"
#define TIMETOFULL     "TimeToFull"
#define TIMETOEMPTY    "TimeToEmpty"
#define SHOULD_DISPLAY "IsPresent"

#define DEGRADED       "PerformanceDegraded"
#define PROFILES       "Profiles"
#define ACTIVE_PROFILE "ActiveProfile"

static std::string get_device_type_description(uint32_t type)
{
    if (type == 2)
    {
        return "Battery ";
    }

    if (type == 3)
    {
        return "UPS ";
    }

    return "";
}

void WayfireBatteryInfo::on_properties_changed(
    const Gio::DBus::Proxy::MapChangedProperties& properties,
    const std::vector<Glib::ustring>& invalidated)
{
    if (!feat_bat || !display_device || !box)
    {
        return;
    }

    try
    {
        bool invalid_icon = false, invalid_details = false;
        bool invalid_state = false;
        for (auto& prop : properties)
        {
            if (prop.first == ICON)
            {
                invalid_icon = true;
            }

            if ((prop.first == TYPE) || (prop.first == STATE) || (prop.first == PERCENTAGE) ||
                (prop.first == TIMETOFULL) || (prop.first == TIMETOEMPTY))
            {
                invalid_details = true;
            }

            if (prop.first == SHOULD_DISPLAY)
            {
                invalid_state = true;
            }
        }

        if (invalid_icon)
        {
            update_icon();
        }

        if (invalid_details)
        {
            update_details();
        }

        if (invalid_state)
        {
            update_state();
        }
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-panel: battery property update failed: " << e.what() << std::endl;
    } catch (...)
    {}
}

void WayfireBatteryInfo::update_icon()
{
    if (!box)
    {
        return;
    }

    try
    {
        if (!feat_bat && feat_modes)
        {
            if (!power_mode.empty())
            {
                icon.set_from_icon_name("power-profile-" + power_mode);
            }
            return;
        }

        if (!display_device)
        {
            return;
        }

        Glib::Variant<Glib::ustring> icon_name;
        display_device->get_cached_property(icon_name, ICON);
        if (icon_name && !icon_name.get().empty())
        {
            icon.set_from_icon_name(icon_name.get());
        }
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-panel: battery icon update failed: " << e.what() << std::endl;
    } catch (...)
    {}
}

static std::string state_descriptions[] = {
    "Unknown", // 0
    "Charging", // 1
    "Discharging", // 2
    "Empty", // 3
    "Fully charged", // 4
    "Pending charge", // 5
    "Pending discharge", // 6
};

static bool is_charging(uint32_t state)
{
    return (state == 1) || (state == 5);
}

static bool is_discharging(uint32_t state)
{
    return (state == 2) || (state == 6);
}

static std::string format_digit(int digit)
{
    return digit <= 9 ? ("0" + std::to_string(digit)) :
           std::to_string(digit);
}

static std::string uint_to_time(int64_t time)
{
    int hrs = time / 3600;
    int min = (time / 60) % 60;

    return format_digit(hrs) + ":" + format_digit(min);
}

void WayfireBatteryInfo::update_details()
{
    if (!feat_bat || !display_device || !box)
    {
        return;
    }

    try
    {
        Glib::Variant<guint32> type;
        display_device->get_cached_property(type, TYPE);

        Glib::Variant<guint32> vstate;
        display_device->get_cached_property(vstate, STATE);
        if (!vstate)
        {
            return;
        }
        uint32_t state = vstate.get();
        if (state >= sizeof(state_descriptions) / sizeof(state_descriptions[0]))
        {
            state = 0;
        }

        Glib::Variant<gdouble> vpercentage;
        display_device->get_cached_property(vpercentage, PERCENTAGE);
        const int pct = vpercentage ? static_cast<int>(vpercentage.get()) : 0;
        auto percentage_string = std::to_string(pct) + "%";

        Glib::Variant<gint64> time_to_full;
        display_device->get_cached_property(time_to_full, TIMETOFULL);

        Glib::Variant<gint64> time_to_empty;
        display_device->get_cached_property(time_to_empty, TIMETOEMPTY);

        std::string description = percentage_string + ", " + state_descriptions[state];
        if (is_charging(state) && time_to_full)
        {
            description += ", " + uint_to_time(time_to_full.get()) + " until full";
        } else if (is_discharging(state) && time_to_empty)
        {
            description += ", " + uint_to_time(time_to_empty.get()) + " remaining";
        }

        const std::string type_desc = type ?
            get_device_type_description(type.get()) : std::string{};
        box->set_tooltip_text(type_desc + description);

        if (status_opt.value() == BATTERY_STATUS_PERCENT)
        {
            label.set_text(percentage_string);
            overlay.remove_overlay(label);
            box->append(label);
        } else if (status_opt.value() == BATTERY_STATUS_FULL)
        {
            label.set_text(description);
            auto children = overlay.get_children();
            if (std::count(children.begin(), children.end(), &label))
            {
                overlay.remove_overlay(label);
            }

            box->append(label);
        } else if (status_opt.value() == BATTERY_STATUS_OVERLAY)
        {
            label.set_text(percentage_string);
            auto children = box->get_children();
            if (std::count(children.begin(), children.end(), &label))
            {
                box->remove(label);
            }

            overlay.add_overlay(label);
        }

        if (status_opt.value() == BATTERY_STATUS_ICON)
        {
            label.hide();
        } else
        {
            label.show();
        }
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-panel: battery details update failed: " << e.what() << std::endl;
    } catch (...)
    {}
}

void WayfireBatteryInfo::update_state()
{
    /* Present-flag changes on desktop/no-battery systems are a no-op. */
    if (!feat_bat || !display_device || !box)
    {
        return;
    }

    try
    {
        Glib::Variant<bool> present;
        display_device->get_cached_property(present, SHOULD_DISPLAY);
        if (!present || !present.get())
        {
            /* Battery disappeared (or was never real) — hide quietly. */
            box->set_visible(false);
            return;
        }
        box->set_visible(true);
        update_details();
        update_icon();
    } catch (...)
    {}
}

bool WayfireBatteryInfo::setup_dbus_power_modes()
{
    try
    {
        auto cancellable = Gio::Cancellable::create();
        if (!connection)
        {
            connection = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SYSTEM, cancellable);
        }
        if (!connection)
        {
            return false;
        }

        powerprofile_proxy = Gio::DBus::Proxy::create_sync(connection, POWER_PROFILE_NAME,
            POWER_PROFILE_PATH,
            POWER_PROFILE_NAME);
        if (!powerprofile_proxy)
        {
            return false;
        }

        Glib::Variant<Glib::ustring> current_profile;
        Glib::Variant<std::vector<std::map<Glib::ustring, Glib::VariantBase>>> profiles;
        powerprofile_proxy->get_cached_property(current_profile, ACTIVE_PROFILE);
        powerprofile_proxy->get_cached_property(profiles, PROFILES);

        if (!profiles || !current_profile)
        {
            powerprofile_proxy.reset();
            return false;
        }

        powerprofile_proxy->signal_properties_changed().connect(
            sigc::mem_fun(*this, &WayfireBatteryInfo::on_upower_properties_changed));
        setup_profiles(profiles.get());
        set_current_profile(current_profile.get());
        return true;
    } catch (const Glib::Error& e)
    {
        /* Common on FreeBSD desktops: power-profiles-daemon not installed. */
        std::cerr << "wf-panel: power profiles unavailable: " << e.what() << std::endl;
    } catch (...)
    {}

    powerprofile_proxy.reset();
    return false;
}

bool WayfireBatteryInfo::setup_dbus_battery()
{
    try
    {
        auto cancellable = Gio::Cancellable::create();
        connection = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SYSTEM, cancellable);
        if (!connection)
        {
            std::cerr << "wf-panel: battery: system D-Bus unavailable" << std::endl;
            return false;
        }

        upower_proxy = Gio::DBus::Proxy::create_sync(connection, UPOWER_NAME,
            "/org/freedesktop/UPower",
            "org.freedesktop.UPower");
        if (!upower_proxy)
        {
            return false;
        }

        display_device = Gio::DBus::Proxy::create_sync(connection,
            UPOWER_NAME,
            DISPLAY_DEVICE,
            "org.freedesktop.UPower.Device");
        if (!display_device)
        {
            return false;
        }

        /*
         * Desktops / FreeBSD without UPower: IsPresent is missing or false.
         * Calling .get() on an empty Glib::Variant triggers GLib-CRITICAL.
         * Never treat "no battery" as a hard failure of the panel.
         */
        Glib::Variant<bool> present;
        display_device->get_cached_property(present, SHOULD_DISPLAY);
        if (!present || !present.get())
        {
            display_device.reset();
            upower_proxy.reset();
            return false;
        }

        disp_dev_sig = display_device->signal_properties_changed().connect(
            sigc::mem_fun(*this, &WayfireBatteryInfo::on_properties_changed));
        return true;
    } catch (const Glib::Error& e)
    {
        /* UPower not installed / not running — expected on desktops. */
        std::cerr << "wf-panel: battery unavailable (desktop or no UPower): "
                  << e.what() << std::endl;
    } catch (...)
    {
        std::cerr << "wf-panel: battery unavailable (unknown error)" << std::endl;
    }

    display_device.reset();
    upower_proxy.reset();
    return false;
}

void WayfireBatteryInfo::update_layout()
{
    /* Widget may be inert when UPower/power-profiles are unavailable (common
     * on FreeBSD desktops). Config reload after theme change must not crash. */
    if (!box)
    {
        return;
    }

    try
    {
        WfOption<std::string> panel_position{"panel/position"};
        const std::string pos = panel_position.value();

        if (pos == PANEL_POSITION_LEFT || pos == PANEL_POSITION_RIGHT)
        {
            box->set_orientation(Gtk::Orientation::VERTICAL);
        } else
        {
            box->set_orientation(Gtk::Orientation::HORIZONTAL);
        }
    } catch (...)
    {
        /* Config mid-reload or missing option — ignore. */
    }
}

void WayfireBatteryInfo::handle_config_reload()
{
    /* Defensive: never let theme/ini reload take down the panel. */
    try
    {
        if (!box)
        {
            return;
        }
        update_layout();
        if (feat_bat)
        {
            update_details();
            update_icon();
        }
    } catch (...)
    {}
}

// TODO: simplify config loading

void WayfireBatteryInfo::init(Gtk::Box *container)
{
    feat_bat   = false;
    feat_modes = false;

    try
    {
        profiles_menu = Gio::Menu::create();
        state_action  = Gio::SimpleAction::create_radio_string("set_profile", "");

        // Battery + power modes are optional. Desktop without UPower must no-op.
        feat_bat   = setup_dbus_battery();
        feat_modes = setup_dbus_power_modes();
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-panel: battery widget init failed: " << e.what() << std::endl;
        feat_bat   = false;
        feat_modes = false;
    } catch (...)
    {
        std::cerr << "wf-panel: battery widget init failed (unknown)" << std::endl;
        feat_bat   = false;
        feat_modes = false;
    }

    // ignore if we can’t do either — do not create UI or touch the panel
    if (!feat_bat && !feat_modes)
    {
        return;
    }

    try
    {
        box = std::make_unique<WayfireMenuWidget>("panel", "battery");

        box->append(overlay);
        overlay.set_child(icon);
        icon.add_css_class("widget-icon");

        if (feat_bat)
        {
            status_opt.set_callback([=] () { update_details(); });
            update_details();
        }

        update_icon();

        if (feat_modes)
        {
            auto actions = Gio::SimpleActionGroup::create();

            state_action->signal_activate().connect([=] (Glib::VariantBase vb)
            {
                // User has requested a change of state. Don't change the UI choice,
                // let the dbus roundtrip happen to be sure.
                if (!connection || !vb.is_of_type(Glib::VariantType("s")))
                {
                    return;
                }
                try
                {
                    Glib::VariantContainerBase params = Glib::Variant<std::tuple<Glib::ustring, Glib::ustring,
                        Glib::VariantBase>>::create({POWER_PROFILE_NAME, ACTIVE_PROFILE, vb});

                    connection->call_sync(
                        POWER_PROFILE_PATH,
                        "org.freedesktop.DBus.Properties",
                        "Set",
                        params,
                        NULL,
                        POWER_PROFILE_NAME,
                        -1,
                        Gio::DBus::CallFlags::NONE,
                        {});
                } catch (const Glib::Error& e)
                {
                    std::cerr << "wf-panel: set power profile failed: " << e.what() << std::endl;
                } catch (...)
                {}
            });

            actions->add_action(state_action);

            box->open_on(1);
            box->insert_action_group("actions", actions);
            box->set_spacing(5);
            box->set_menu_model(profiles_menu);
        }

        container->append(*box);

        icon.property_scale_factor().signal_changed()
            .connect(sigc::mem_fun(*this, &WayfireBatteryInfo::update_icon));

        update_layout();
    } catch (const Glib::Error& e)
    {
        std::cerr << "wf-panel: battery UI setup failed: " << e.what() << std::endl;
        box.reset();
        feat_bat   = false;
        feat_modes = false;
    } catch (...)
    {
        std::cerr << "wf-panel: battery UI setup failed (unknown)" << std::endl;
        box.reset();
        feat_bat   = false;
        feat_modes = false;
    }
}

void WayfireBatteryInfo::on_upower_properties_changed(
    const Gio::DBus::Proxy::MapChangedProperties& properties,
    const std::vector<Glib::ustring>& invalidated)
{
    if (!feat_modes)
    {
        return;
    }

    try
    {
        for (auto& prop : properties)
        {
            if (prop.first == ACTIVE_PROFILE)
            {
                if (prop.second.is_of_type(Glib::VariantType("s")))
                {
                    auto value_string =
                        Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(prop.second).get();
                    set_current_profile(value_string);
                }
            } else if (prop.first == PROFILES)
            {
                // I've been unable to find a way to change possible profiles on the fly, so cannot confirm this
                // works at all.
                auto value = Glib::VariantBase::cast_dynamic<Glib::Variant<std::vector<std::map<Glib::ustring,
                    Glib::VariantBase>>>>(prop.second);
                setup_profiles(value.get());
            }

            // TODO Consider watching for "Performance Degraded" events too, but we currently have no way to
            // output this additional information
        }
    } catch (...)
    {}
}

void WayfireBatteryInfo::set_current_profile(Glib::ustring profile)
{
    power_mode = profile;
    if (state_action)
    {
        try
        {
            state_action->set_state(Glib::Variant<Glib::ustring>::create(profile));
        } catch (...)
        {}
    }
    update_icon();
}

void WayfireBatteryInfo::setup_profiles(std::vector<std::map<Glib::ustring, Glib::VariantBase>> profiles)
{
    if (!profiles_menu)
    {
        return;
    }

    try
    {
        profiles_menu->remove_all();
        for (auto profile : profiles)
        {
            if (profile.count("Profile") == 1)
            {
                Glib::VariantBase value = profile.at("Profile");
                if (value.is_of_type(Glib::VariantType("s")))
                {
                    auto value_string =
                        Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
                    auto item = Gio::MenuItem::create(value_string, "noactionyet");

                    item->set_action_and_target("actions.set_profile",
                        Glib::Variant<Glib::ustring>::create(value_string));
                    profiles_menu->append_item(item);
                }
            }
        }
    } catch (...)
    {}
}

WayfireBatteryInfo::~WayfireBatteryInfo()
{
    try
    {
        btn_sig.disconnect();
        disp_dev_sig.disconnect();
    } catch (...)
    {}
}
