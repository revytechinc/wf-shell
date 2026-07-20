#pragma once

/**
 * User config ensure — TAOCP pure-ish path helpers for wf-settings.
 *
 * When the user has no prefs yet, Settings must:
 *   1) create ~/.config (or $XDG_CONFIG_HOME) if missing
 *   2) create the config file (seed from system package default when present)
 *   3) report a clear error if it cannot create (permissions, full disk, etc.)
 *
 * Never invent compositor state. Fail-soft: return false + error string.
 */

#include <string>

namespace wf_shell
{

/** Result of ensure_user_config_file. */
struct UserConfigEnsure
{
    bool ok = false;
    std::string path;
    std::string error;       /* set when !ok */
    bool created_dir  = false;
    bool created_file = false;
    bool seeded_from_system = false;
};

/**
 * Ensure parent directories exist for path (mkdir -p).
 * Fails with a plain-English error if not writable.
 */
bool ensure_parent_directories(const std::string& path, std::string *error = nullptr);

/**
 * Ensure config file exists and is writable.
 * - Creates parent dirs if needed
 * - If file missing and seed_path exists: copy seed
 * - If file missing and no seed: create empty file (or header_comment)
 * - If file exists: verify writable (open for append)
 * - On failure: ok=false, error filled
 */
UserConfigEnsure ensure_user_config_file(const std::string& path,
    const std::string& seed_path = {},
    const std::string& header_if_empty = {});

/** Package system seeds (OS-aware). Empty if not installed. */
std::string system_wf_shell_ini_path();
std::string system_wayfire_ini_path();

/** User paths (respect env overrides). */
std::string user_wf_shell_ini_path();
std::string user_wayfire_ini_path();
std::string user_shell_json_path();

/**
 * Ensure the three primary user config artifacts Settings may write.
 * On partial failure returns false and first error.
 */
bool ensure_settings_user_configs(std::string *error = nullptr);

} // namespace wf_shell
