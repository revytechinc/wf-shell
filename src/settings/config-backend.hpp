#pragma once

/**
 * Shared config backend for settings pages + plugin browser.
 *
 * CRITICAL SAFETY: never dump the full in-memory config_manager (all XML
 * defaults + compound options) back over the user's wayfire.ini. That rewrites
 * the whole file and has crashed live Wayfire sessions on reload.
 *
 * Saves are dirty-key only → targeted INI upserts + backup first.
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wayfire/config/config-manager.hpp>
#include <wayfire/config/file.hpp>

namespace wf_settings
{

enum class ConfigDomain
{
    Wayfire,
    Shell,
};

struct ConfigBackend
{
    wf::config::config_manager_t wayfire;
    wf::config::config_manager_t shell;
    std::string wayfire_ini;
    std::string shell_ini;
    std::string shell_json;
    bool wayfire_ok = false;
    bool shell_ok = false;

    static ConfigBackend& instance();

    void reload();

    /**
     * Record a wayfire option the user changed. Does not write disk yet.
     * Memory manager is updated for UI consistency.
     */
    bool set_wayfire_option(const std::string& section, const std::string& key,
        const std::string& value, std::string *error = nullptr);

    /**
     * Write only dirty wayfire keys (section/key upsert). Backs up ini first.
     * Never calls save_configuration_to_file on the full manager.
     */
    bool save_wayfire(std::string *error = nullptr);

    bool save_shell_option(const std::string& section, const std::string& key,
        const std::string& value, std::string *error = nullptr);

    std::shared_ptr<wf::config::option_base_t> get_option(ConfigDomain dom,
        const std::string& section, const std::string& name);

    std::vector<std::string> wayfire_section_names() const;
    std::vector<std::string> shell_section_names() const;

    bool has_dirty_wayfire() const { return !dirty_wayfire.empty(); }
    size_t dirty_wayfire_count() const;

  private:
    ConfigBackend() = default;

    /** section → key → value */
    std::map<std::string, std::map<std::string, std::string>> dirty_wayfire;
};

std::string default_wayfire_ini();
std::string default_shell_ini();
std::string wayfire_metadata_dir();
std::string shell_metadata_dir();

} // namespace wf_settings
