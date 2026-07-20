#include "app.hpp"

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
                "wf-settings — GTK4 desktop settings (WCM replacement)\n"
                "  -c, --config FILE         wayfire.ini path\n"
                "  -s, --shell-config FILE   wf-shell.ini path\n"
                "  -p, --plugin NAME         open plugin section in sidebar\n"
                "  JSON config: ~/.config/wf-shell/config.json (overrides ini)\n";
            return 0;
        }
    }

    auto app = wf_settings::SettingsApp();
    if (!start_plugin.empty())
    {
        app.set_start_plugin(start_plugin);
    }
    return app.run(argc, argv);
}
