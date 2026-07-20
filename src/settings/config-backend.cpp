#include "config-backend.hpp"

#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace wf_settings
{
namespace
{

std::string env_or(const char *k, const std::string& fallback)
{
    const char *v = std::getenv(k);
    return (v && v[0]) ? std::string(v) : fallback;
}

std::string home_config(const std::string& leaf)
{
    const char *xh = std::getenv("XDG_CONFIG_HOME");
    if (xh && xh[0])
    {
        return std::string(xh) + "/" + leaf;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/" + leaf : leaf;
}

bool backup_file(const std::string& path, std::string *error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return true; /* nothing to back up */
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string bak = path + ".bak-" + std::to_string(ms);
    fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (error)
        {
            *error = "backup failed: " + ec.message();
        }
        return false;
    }
    /* Keep one rolling easy restore path */
    fs::copy_file(path, path + ".bak-last", fs::copy_options::overwrite_existing, ec);
    return true;
}

} // namespace

std::string default_wayfire_ini()
{
    return env_or("WAYFIRE_CONFIG_FILE", home_config("wayfire.ini"));
}

std::string default_shell_ini()
{
    return env_or("WF_SHELL_CONFIG_FILE", home_config("wf-shell.ini"));
}

std::string wayfire_metadata_dir()
{
    if (const char *p = std::getenv("WAYFIRE_PLUGIN_XML_PATH"); p && p[0])
    {
        std::string s = p;
        auto colon = s.find(':');
        return colon == std::string::npos ? s : s.substr(0, colon);
    }
    return "/usr/local/share/wayfire/metadata";
}

std::string shell_metadata_dir()
{
#ifdef METADATA_DIR
    return METADATA_DIR;
#else
    const char *h = std::getenv("HOME");
    if (h)
    {
        std::string local = std::string(h) + "/.local/share/wf-shell/metadata";
        if (std::filesystem::is_directory(local))
        {
            return local;
        }
    }
    return "/usr/local/share/wf-shell/metadata";
#endif
}

ConfigBackend& ConfigBackend::instance()
{
    static ConfigBackend b;
    return b;
}

void ConfigBackend::reload()
{
    wayfire_ini = default_wayfire_ini();
    shell_ini   = default_shell_ini();
    shell_json  = wf_shell::shell_json_config_path();
    dirty_wayfire.clear();

    try
    {
        std::vector<std::string> wdirs;
        if (const char *p = std::getenv("WAYFIRE_PLUGIN_XML_PATH"); p && p[0])
        {
            std::stringstream ss(p);
            std::string entry;
            while (std::getline(ss, entry, ':'))
            {
                if (!entry.empty())
                {
                    wdirs.push_back(entry);
                }
            }
        }
        wdirs.push_back(wayfire_metadata_dir());
        wayfire = wf::config::build_configuration(wdirs,
            "/usr/local/etc/wayfire/defaults.ini", wayfire_ini);
        wf::config::load_configuration_options_from_file(wayfire, wayfire_ini);
        wayfire_ok = true;
    } catch (const std::exception& e)
    {
        std::cerr << "wf-settings: wayfire config load failed: " << e.what() << "\n";
        wayfire_ok = false;
    }

    try
    {
        std::vector<std::string> sdirs{shell_metadata_dir()};
        shell = wf::config::build_configuration(sdirs,
            "/usr/local/etc/wayfire/wf-shell-defaults.ini", shell_ini);
        wf::config::load_configuration_options_from_file(shell, shell_ini);
        wf_shell::ShellJsonConfig jcfg;
        std::string jerr;
        if (wf_shell::load_shell_json_config(shell_json, jcfg, &jerr))
        {
            wf_shell::apply_shell_json_to_config_manager(jcfg, shell, nullptr);
        }
        shell_ok = true;
    } catch (const std::exception& e)
    {
        std::cerr << "wf-settings: shell config load failed: " << e.what() << "\n";
        shell_ok = false;
    }
}

bool ConfigBackend::set_wayfire_option(const std::string& section, const std::string& key,
    const std::string& value, std::string *error)
{
    auto opt = wayfire.get_option(section + "/" + key);
    if (opt)
    {
        if (!opt->set_value_str(value))
        {
            if (error)
            {
                *error = "rejected value for " + section + "/" + key;
            }
            return false;
        }
    }
    /* Always dirty-track so Save can write even if option object missing */
    dirty_wayfire[section][key] = value;
    return true;
}

size_t ConfigBackend::dirty_wayfire_count() const
{
    size_t n = 0;
    for (const auto& sec : dirty_wayfire)
    {
        n += sec.second.size();
    }
    return n;
}

bool ConfigBackend::save_wayfire(std::string *error)
{
    if (dirty_wayfire.empty())
    {
        if (error)
        {
            *error = "nothing to save (no changes)";
        }
        /* Not a hard failure — UI can treat as soft message */
        return true;
    }

    if (!backup_file(wayfire_ini, error))
    {
        return false;
    }

    for (const auto& sec : dirty_wayfire)
    {
        if (!wf_shell::ini_set_many(wayfire_ini, sec.first, sec.second))
        {
            if (error)
            {
                *error = "failed writing section [" + sec.first + "] to " + wayfire_ini;
            }
            return false;
        }
    }

    dirty_wayfire.clear();
    return true;
}

bool ConfigBackend::save_shell_option(const std::string& section, const std::string& key,
    const std::string& value, std::string *error)
{
    auto opt = shell.get_option(section + "/" + key);
    if (opt)
    {
        opt->set_value_str(value);
    }
    return wf_shell::settings_save_section(section, {{key, value}}, error);
}

std::shared_ptr<wf::config::option_base_t> ConfigBackend::get_option(ConfigDomain dom,
    const std::string& section, const std::string& name)
{
    auto& mgr = (dom == ConfigDomain::Wayfire) ? wayfire : shell;
    return mgr.get_option(section + "/" + name);
}

std::vector<std::string> ConfigBackend::wayfire_section_names() const
{
    std::vector<std::string> names;
    for (auto& s : wayfire.get_all_sections())
    {
        if (s)
        {
            names.push_back(s->get_name());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> ConfigBackend::shell_section_names() const
{
    std::vector<std::string> names;
    for (auto& s : shell.get_all_sections())
    {
        if (s)
        {
            names.push_back(s->get_name());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace wf_settings
