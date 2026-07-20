#pragma once

/**
 * Shell JSON configuration (primary going forward).
 *
 * Path: $XDG_CONFIG_HOME/wf-shell/config.json  (default ~/.config/wf-shell/config.json)
 *
 * Load order for the panel:
 *   1. XML defaults + sysconf
 *   2. Legacy wf-shell.ini (still supported)
 *   3. config.json overrides INI  ← wins
 *
 * Settings writes JSON first, then dual-writes INI for legacy tools until INI is gone.
 *
 * Schema (v1):
 * {
 *   "version": 1,
 *   "panel": { "css_path": "...", "position": "top", ... },
 *   "background": { ... },
 *   "dock": { ... },
 *   "mcp": {
 *     "enabled": false,
 *     "servers": [
 *       {
 *         "id": "honcho",
 *         "name": "Honcho",
 *         "enabled": true,
 *         "transport": "stdio",
 *         "command": "honcho-mcp",
 *         "args": [],
 *         "env": {},
 *         "notes": "AI memory / peers"
 *       }
 *     ]
 *   }
 * }
 */

#include <map>
#include <string>
#include <vector>

#include <wayfire/config/config-manager.hpp>

namespace wf_shell
{

struct McpServerConfig
{
    std::string id;
    std::string name;
    bool enabled = true;
    std::string transport = "stdio"; /* stdio | sse | http — future */
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::string notes;
};

struct ShellJsonConfig
{
    int version = 1;
    /** section → key → value (string form, same as INI values) */
    std::map<std::string, std::map<std::string, std::string>> sections;
    bool mcp_enabled = false;
    std::vector<McpServerConfig> mcp_servers;
    bool loaded_from_disk = false;
    std::string path;
};

/** Default path ~/.config/wf-shell/config.json */
std::string shell_json_config_path();

/** Parse JSON text into ShellJsonConfig (pure). */
bool parse_shell_json_config(const std::string& text, ShellJsonConfig& out,
    std::string *error = nullptr);

/** Serialize to pretty JSON string. */
std::string serialize_shell_json_config(const ShellJsonConfig& cfg);

/** Load from disk if present. */
bool load_shell_json_config(const std::string& path, ShellJsonConfig& out,
    std::string *error = nullptr);

/** Atomic-ish write (write temp + rename when possible). */
bool save_shell_json_config(const std::string& path, const ShellJsonConfig& cfg,
    std::string *error = nullptr);

/**
 * Apply section key/values onto live wf-config manager (JSON wins).
 * Unknown options are skipped with optional warning list.
 */
void apply_shell_json_to_config_manager(const ShellJsonConfig& cfg,
    wf::config::config_manager_t& manager,
    std::vector<std::string> *warnings = nullptr);

/** Set one key in memory config (creates section map). */
void shell_json_set(ShellJsonConfig& cfg, const std::string& section,
    const std::string& key, const std::string& value);

std::string shell_json_get(const ShellJsonConfig& cfg, const std::string& section,
    const std::string& key, const std::string& default_val = {});

/**
 * Settings save helper: update JSON section keys + dual-write same keys to INI.
 * Ensures JSON is created if missing.
 */
bool settings_save_section(const std::string& section,
    const std::map<std::string, std::string>& kv,
    std::string *error = nullptr);

/** Ensure default mcp stub exists when creating first JSON file. */
void ensure_default_mcp_stub(ShellJsonConfig& cfg);

} // namespace wf_shell
