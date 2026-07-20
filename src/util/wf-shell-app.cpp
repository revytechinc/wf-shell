#include "wf-shell-app.hpp"
#include "apply-gate.hpp"
#include "session-env.hpp"
#include "shell-json-config.hpp"
#include <glibmm/main.h>
#include <sys/inotify.h>
#include <gdk/wayland/gdkwayland.h>
#include <gio/gio.h>
#include <iostream>
#include <filesystem>
#include <memory>
#include <wayfire/config/file.hpp>
#include <wf-option-wrap.hpp>
#include <gtk-utils.hpp>

#include <unistd.h>

namespace
{
/* Theme dropdown thrash: coalesce inotify reloads. */
constexpr unsigned kConfigReloadDebounceMs = 250;
} // namespace

std::string WayfireShellApp::get_config_file()
{
    if (cmdline_config.has_value())
    {
        return cmdline_config.value();
    }

    std::string config_dir;

    char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home == NULL)
    {
        config_dir = std::string(getenv("HOME")) + "/.config";
    } else
    {
        config_dir = std::string(config_home);
    }

    return config_dir + "/wf-shell.ini";
}

std::string WayfireShellApp::get_css_config_dir()
{
    if (cmdline_css.has_value())
    {
        return cmdline_config.value();
    }

    std::string config_dir;

    char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home == NULL)
    {
        config_dir = std::string(getenv("HOME")) + "/.config";
    } else
    {
        config_dir = std::string(config_home);
    }

    auto css_directory = config_dir + "/wf-shell/css/";
    /* Ensure it exists */
    std::filesystem::create_directories(css_directory);

    return css_directory;
}

void WayfireShellApp::on_css_reload()
{
    /*
     * VALIDATE → LOAD NEW → SWAP (never clear-first).
     * Clearing CSS before load left the panel unstyled mid-reload and
     * contributed to thrash/crash when Settings live-applied themes.
     */
    if (css_reload_busy)
    {
        wf_shell::gate_log("css_reload", "reentrant call ignored");
        return;
    }
    css_reload_busy = true;

    auto display = Gdk::Display::get_default();
    if (!display)
    {
        std::cerr << "wf-shell:css_reload: no Gdk display — skip\n";
        css_reload_busy = false;
        return;
    }

    std::vector<std::pair<Glib::RefPtr<Gtk::CssProvider>, int>> staged;
    auto stage = [&] (const std::string& file, int priority) -> bool {
        if (file.empty())
        {
            return true;
        }
        auto gate = wf_shell::validate_theme_css_path(file);
        if (!gate.ok)
        {
            std::cerr << "wf-shell:css_reload: skip invalid \"" << file
                      << "\" — " << gate.summary() << "\n";
            return false;
        }
        auto css_provider = load_css_from_path(file);
        if (!css_provider)
        {
            std::cerr << "wf-shell:css_reload: load failed \"" << file
                      << "\" — keep previous styles\n";
            return false;
        }
        staged.emplace_back(css_provider, priority);
        wf_shell::gate_log("css_reload", "staged " + file +
            " prio=" + std::to_string(priority));
        return true;
    };

    /* Base chrome */
    const std::string def = std::string(RESOURCEDIR) + "/css/default.css";
    if (!stage(def, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION))
    {
        std::cerr << "wf-shell:css_reload: default.css failed — abort swap "
                     "(keeping old providers)\n";
        css_reload_busy = false;
        return;
    }

    /* Optional per-user CSS dir (not full theme packs) */
    std::string ext(".css");
    try
    {
        for (auto & p : std::filesystem::directory_iterator(get_css_config_dir()))
        {
            if (p.path().extension() == ext)
            {
                (void)stage(p.path().string(), GTK_STYLE_PROVIDER_PRIORITY_USER);
            }
        }
    } catch (const std::exception& e)
    {
        std::cerr << "wf-shell:css_reload: css dir scan: " << e.what() << "\n";
    }

    /* Theme pack path from config — only after validate */
    std::string custom_css;
    try
    {
        auto custom_css_config = WfOption<std::string>{"panel/css_path"};
        custom_css = custom_css_config;
    } catch (...)
    {
        custom_css.clear();
    }

    if (!custom_css.empty())
    {
        if (!stage(custom_css, GTK_STYLE_PROVIDER_PRIORITY_USER))
        {
            std::cerr << "wf-shell:css_reload: custom theme invalid, applying "
                         "base styles only (not leaving panel unstyled)\n";
        }
    }

    /* Atomic swap: remove old only after new providers are ready */
    for (auto css_provider : css_rules)
    {
        Gtk::StyleContext::remove_provider_for_display(display, css_provider);
    }
    css_rules.clear();
    for (auto& [prov, prio] : staged)
    {
        Gtk::StyleContext::add_provider_for_display(display, prov, prio);
        css_rules.push_back(prov);
    }
    last_css_path = custom_css;
    wf_shell::gate_log("css_reload", "swap complete providers=" +
        std::to_string(css_rules.size()) + " css_path=\"" + custom_css + "\"");
    css_reload_busy = false;
}

void WayfireShellApp::clear_css_rules()
{
    auto display = Gdk::Display::get_default();
    if (!display)
    {
        css_rules.clear();
        return;
    }
    for (auto css_provider : css_rules)
    {
        Gtk::StyleContext::remove_provider_for_display(display, css_provider);
    }
    css_rules.clear();
}

void WayfireShellApp::add_css_file(std::string file, int priority)
{
    auto display = Gdk::Display::get_default();
    if (!display || file.empty())
    {
        return;
    }
    auto gate = wf_shell::validate_theme_css_path(file);
    if (!gate.ok)
    {
        std::cerr << "wf-shell: ignoring css_path \"" << file
                  << "\" — " << gate.summary() << "\n";
        return;
    }
    auto css_provider = load_css_from_path(file);
    if (!css_provider)
    {
        std::cerr << "wf-shell: ignoring css_path \"" << file
                  << "\" (not loaded); keeping prior styles\n";
        return;
    }
    Gtk::StyleContext::add_provider_for_display(
        display, css_provider, priority);
    css_rules.push_back(css_provider);
}

bool WayfireShellApp::parse_cfgfile(const Glib::ustring & option_name,
    const Glib::ustring & value, bool has_value)
{
    std::cout << "%%%%%%%%%%%%%%%%%%%%%%%" << std::endl;
    std::cout << "Using custom config file " << value << std::endl;
    cmdline_config = value;
    return true;
}

bool WayfireShellApp::parse_cssfile(const Glib::ustring & option_name,
    const Glib::ustring & value, bool has_value)
{
    std::cout << "Using custom css directory " << value << std::endl;
    cmdline_css = value;
    return true;
}

#define INOT_BUF_SIZE (1024 * sizeof(inotify_event))
char buf[INOT_BUF_SIZE];

static void do_reload_css(WayfireShellApp *app)
{
    try
    {
        app->on_css_reload();
    } catch (const std::exception& e)
    {
        std::cerr << "wf-shell:css_reload: exception: " << e.what() << "\n";
    } catch (...)
    {
        std::cerr << "wf-shell:css_reload: unknown exception\n";
    }
}

/* Reload file and add next inotify watch */
static void do_reload_config_now(WayfireShellApp *app)
{
    if (app->config_reload_busy)
    {
        wf_shell::gate_log("config_reload", "already busy — skip nested");
        return;
    }
    app->config_reload_busy = true;
    wf_shell::gate_log("config_reload", "begin file=" + app->get_config_file());

    std::string prev_css = app->last_css_path;

    try
    {
        /* 1) Legacy INI (still supported) */
        wf::config::load_configuration_options_from_file(
            app->config, app->get_config_file());

        /* 2) JSON overrides INI (primary going forward) */
        try
        {
            wf_shell::ShellJsonConfig jcfg;
            std::string jerr;
            auto jpath = wf_shell::shell_json_config_path();
            if (wf_shell::load_shell_json_config(jpath, jcfg, &jerr))
            {
                std::vector<std::string> warns;
                wf_shell::apply_shell_json_to_config_manager(jcfg, app->config, &warns);
                for (const auto& w : warns)
                {
                    std::cerr << "wf-shell: config_reload warn: " << w << "\n";
                }
            } else if (wf_shell::apply_debug_enabled() && !jerr.empty())
            {
                std::cerr << "wf-shell: config_reload json: " << jerr << "\n";
            }
        } catch (const std::exception& e)
        {
            std::cerr << "wf-shell: json config apply failed: " << e.what() << "\n";
        }

        app->on_config_reload();

        /* CSS only if theme path changed (or first load empty→set). */
        std::string new_css;
        try
        {
            new_css = static_cast<std::string>(WfOption<std::string>{"panel/css_path"});
        } catch (...)
        {
            new_css.clear();
        }
        if (new_css != prev_css)
        {
            wf_shell::gate_log("config_reload", "css_path changed \"" + prev_css +
                "\" → \"" + new_css + "\" — reload CSS");
            do_reload_css(app);
        } else
        {
            wf_shell::gate_log("config_reload", "css_path unchanged — skip CSS reload");
        }
    } catch (const std::exception& e)
    {
        std::cerr << "wf-shell:config_reload: exception: " << e.what() << "\n";
    } catch (...)
    {
        std::cerr << "wf-shell:config_reload: unknown exception\n";
    }

    app->config_reload_busy = false;
    wf_shell::gate_log("config_reload", "end");
}

static void schedule_reload_config(WayfireShellApp *app)
{
    /* Debounce: rapid theme cycling must not stack reloads. */
    if (app->config_reload_debounce.connected())
    {
        app->config_reload_debounce.disconnect();
    }
    app->config_reload_debounce = Glib::signal_timeout().connect(
        [app] () {
            do_reload_config_now(app);
            return false; /* one-shot */
        },
        kConfigReloadDebounceMs);
    wf_shell::gate_log("config_reload", "scheduled in " +
        std::to_string(kConfigReloadDebounceMs) + "ms");
}

/* Handle inotify event */
static bool handle_inotify_event(WayfireShellApp *app, Glib::IOCondition cond)
{
    /* Read once (fd is edge-triggered via Glib IO); debounce the actual work. */
    (void)read(app->inotify_fd, buf, INOT_BUF_SIZE);
    (void)cond;
    schedule_reload_config(app);
    return true;
}

static bool handle_css_inotify_event(WayfireShellApp *app, Glib::IOCondition cond)
{
    (void)read(app->inotify_css_fd, buf, INOT_BUF_SIZE);
    (void)cond;
    if (app->config_reload_debounce.connected())
    {
        app->config_reload_debounce.disconnect();
    }
    app->config_reload_debounce = Glib::signal_timeout().connect(
        [app] () {
            do_reload_css(app);
            return false;
        },
        kConfigReloadDebounceMs);
    return true;
}

static void registry_add_object(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    auto app = static_cast<WayfireShellApp*>(data);
    if (strcmp(interface, zwf_shell_manager_v2_interface.name) == 0)
    {
        app->wf_shell_manager = (zwf_shell_manager_v2*)wl_registry_bind(registry, name,
            &zwf_shell_manager_v2_interface, std::min(version, 2u));
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry,
    uint32_t name)
{}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

void WayfireShellApp::on_activate()
{
    if (activated)
    {
        return;
    }

    /*
     * Pre-flight: discover missing session env (WAYLAND_DISPLAY, XDG_RUNTIME_DIR,
     * DBUS_SESSION_BUS_ADDRESS, PATH, …) so menu/launcher children inherit a
     * runnable environment even when the panel was started with a minimal env.
     */
    try
    {
        wf_shell::ensure_session_env(true);
    } catch (...)
    {
        std::cerr << "wf-shell: session-env preflight failed (continuing)\n";
    }

    activated = true;
    app->hold();

    // load wf-shell if available
    auto gdk_display = gdk_display_get_default();
    auto wl_display  = gdk_wayland_display_get_wl_display(gdk_display);
    if (!wl_display)
    {
        std::cerr << "Failed to connect to wayland display!" <<
            " Are you sure you are running a wayland compositor?" << std::endl;
        std::exit(-1);
    }

    wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(wl_display);

    std::vector<std::string> xmldirs(1, METADATA_DIR);

    // setup config
    this->config = wf::config::build_configuration(
        xmldirs, SYSCONF_DIR "/wayfire/wf-shell-defaults.ini",
        get_config_file());

    inotify_fd = inotify_init();
    do_reload_config_now(this);
    inotify_css_fd = inotify_init();
    do_reload_css(this);

    inotify_add_watch(inotify_fd,
        get_config_file().c_str(),
        IN_CLOSE_WRITE);
    /* Watch JSON config dir (create/modify of config.json) */
    try
    {
        auto jpath = wf_shell::shell_json_config_path();
        auto jdir  = std::filesystem::path(jpath).parent_path().string();
        std::filesystem::create_directories(jdir);
        inotify_add_watch(inotify_fd, jdir.c_str(),
            IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO);
    } catch (...)
    {}
    inotify_add_watch(inotify_css_fd,
        get_css_config_dir().c_str(),
        IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE);
    Glib::signal_io().connect(
        sigc::bind<0>(&handle_inotify_event, this),
        inotify_fd, Glib::IOCondition::IO_IN | Glib::IOCondition::IO_HUP);
    Glib::signal_io().connect(
        sigc::bind<0>(&handle_css_inotify_event, this),
        inotify_css_fd, Glib::IOCondition::IO_IN | Glib::IOCondition::IO_HUP);

    if (!alternative_monitors)
    {
        // Hook up monitor tracking
        auto display = Gdk::Display::get_default();
        auto monitor_list = display->get_monitors();
        monitor_list->signal_items_changed().connect(sigc::mem_fun(*this,
            &WayfireShellApp::output_list_updated));

        // initial monitors
        int num_monitors = monitor_list->get_n_items();
        for (int i = 0; i < num_monitors; i++)
        {
            auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitor_list->get_object(i));
            add_output(monitor);
        }
    }
}

bool WayfireShellApp::sanity_check_outputs()
{
    auto display = Gdk::Display::get_default();
    auto monitor_list = display->get_monitors();

    for (guint count = 0; count < monitor_list->get_n_items(); count++)
    {
        auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitor_list->get_object(count));
        std::string output_name = monitor->get_connector();
        if (output_name.empty())
        {
            return false;
        }
    }

    return true;
}

void WayfireShellApp::output_list_updated(const int pos, const int rem, const int add)
{
    /* XXX: Workaround bug that causes a new output to have
     * no name. We use the name for various reasons so we need
     * it to be valid when signaling new output to the apps.
     * We set a timer for 100ms, then print a warning if the
     * timeout fired.
     *
     * Moved up here because context-looping as before would cause a crash
     * on quick plug/unplug
     */
    debounce_monitor_hotplug.disconnect();

    if (!sanity_check_outputs())
    {
        debounce_monitor_hotplug = Glib::signal_idle().connect([=] ()
        {
            output_list_updated(0, 0, 0);
            return G_SOURCE_REMOVE;
        });
        return;
    }

    /* XXX: End workaround*/

    auto display = Gdk::Display::get_default();
    auto monitor_list = display->get_monitors();

    for (guint count = 0; count < monitor_list->get_n_items(); count++)
    {
        auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(monitor_list->get_object(count));

        std::string output_name = monitor->get_connector();
        if (monitor->get_connector() == live_preview_output_name)
        {
            live_preview_output = gdk_wayland_monitor_get_wl_output(monitor->gobj());
            monitor->signal_invalidate().connect([=]
            {
                live_preview_output = nullptr;
            });
            continue;
        }

        add_output(monitor);
    }
}

void WayfireShellApp::add_output(GMonitor monitor)
{
    auto it = std::find_if(monitors.begin(), monitors.end(),
        [monitor] (auto& output) { return output->monitor == monitor; });

    if (it != monitors.end())
    {
        // We have an entry for this output
        return;
    }

    // Remove self when unplugged
    monitor->signal_invalidate().connect([=]
    {
        rem_output(monitor);
        monitor_list_changed.emit();
    });
    // Add to list
    monitors.push_back(
        std::make_unique<WayfireOutput>(monitor, this->wf_shell_manager));
    handle_new_output(monitors.back().get());
    monitor_list_changed.emit();
}

void WayfireShellApp::rem_output(GMonitor monitor)
{
    auto it = std::find_if(monitors.begin(), monitors.end(),
        [monitor] (auto& output) { return output->monitor == monitor; });

    handle_output_removed(it->get());
    if (it != monitors.end())
    {
        monitors.erase(it);
    } else
    {
        std::cerr << "rem_output didn't find monitor" << std::endl;
    }
}

Gio::Application::Flags WayfireShellApp::get_extra_application_flags()
{
    return Gio::Application::Flags::NONE;
}

std::vector<std::unique_ptr<WayfireOutput>>*WayfireShellApp::get_wayfire_outputs()
{
    return &monitors;
}

WayfireShellApp::WayfireShellApp()
{
    live_preview_output = nullptr;
    live_preview_output_name = "live-preview";
}

void WayfireShellApp::init_app()
{
    std::cout << "setting up" << std::endl;
    app = Gtk::Application::create(
        this->get_application_name(), Gio::Application::Flags::NONE | this->get_extra_application_flags());
    app->signal_activate().connect(
        sigc::mem_fun(*this, &WayfireShellApp::on_activate));
    app->add_main_option_entry(
        sigc::mem_fun(*this, &WayfireShellApp::parse_cfgfile),
        "config", 'c', "config file to use", "file");
    app->add_main_option_entry(
        sigc::mem_fun(*this, &WayfireShellApp::parse_cssfile),
        "css", 's', "css style directory to use", "directory");
    this->command_line();
    // Activate app after parsing command line
    app->signal_command_line().connect_notify([=] (auto&)
    {
        app->activate();
    });
}

WayfireShellApp::~WayfireShellApp()
{
    debounce_monitor_hotplug.disconnect();
}

std::unique_ptr<WayfireShellApp> WayfireShellApp::instance;
WayfireShellApp& WayfireShellApp::get()
{
    return *instance;
}

void WayfireShellApp::run(int argc, char **argv)
{
    app->run(argc, argv);
}

/* -------------------------- WayfireOutput --------------------------------- */
WayfireOutput::WayfireOutput(const GMonitor& monitor,
    zwf_shell_manager_v2 *zwf_manager)
{
    this->monitor = monitor;
    this->wo = gdk_wayland_monitor_get_wl_output(monitor->gobj());

    if (zwf_manager)
    {
        this->output =
            zwf_shell_manager_v2_get_wf_output(zwf_manager, this->wo);
    } else
    {
        this->output = nullptr;
    }
}

WayfireOutput::~WayfireOutput()
{
    if (this->output)
    {
        zwf_output_v2_destroy(this->output);
    }
}

sigc::signal<void()> WayfireOutput::toggle_menu_signal()
{
    return m_toggle_menu_signal;
}
