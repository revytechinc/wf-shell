#include "app.hpp"
#include "startup-gate.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    std::string start_plugin;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if ((a == "-c" || a == "--config") && i + 1 < argc)
        {
            setenv("WAYFIRE_CONFIG_FILE", argv[++i], 1);
        } else if ((a == "-s" || a == "--shell-config") && i + 1 < argc)
        {
            setenv("WF_SHELL_CONFIG_FILE", argv[++i], 1);
        } else if ((a == "-p" || a == "--plugin") && i + 1 < argc)
        {
            start_plugin = argv[++i];
        } else if (a == "-h" || a == "--help")
        {
            std::cout <<
                "wf-settings — Settings for your Wayfire desktop\n"
                "  -c, --config FILE         wayfire.ini path\n"
                "  -s, --shell-config FILE   wf-shell.ini path\n"
                "  -p, --plugin NAME         open a section by name\n"
                "\n"
                "Plain language: open this from the panel while logged in.\n"
                "It will not touch your monitors unless you ask.\n";
            return 0;
        }
    }

    /* Boundary: sanitize env + gate before Gtk constructs a display. */
    auto gate = wf_settings::prepare_settings_process_env();
    if (!gate.safe_to_open())
    {
        std::cerr << "wf-settings: " << gate.user_summary() << "\n";
        for (const auto& w : gate.warnings)
        {
            std::cerr << "  note: " << w << "\n";
        }
        return 2;
    }

    auto app = wf_settings::SettingsApp();
    if (!start_plugin.empty())
    {
        app.set_start_plugin(start_plugin);
    }
    return app.run(argc, argv);
}
