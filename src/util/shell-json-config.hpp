#pragma once

/**
 * Shell JSON configuration — TAOCP / fail-soft design.
 *
 * Path: $XDG_CONFIG_HOME/wf-shell/config.json
 *   last-good: config.json.last-good
 *   quarantine: wf-shell/quarantine/config.json.bad-<stamp>
 *
 * Pure core:
 *   - parse_shell_json_config / validate_shell_json_text  (no I/O)
 *   - serialize_shell_json_config / make_baseline_shell_json
 *
 * Boundary (I/O):
 *   - load: validate → parse; on hard fail try last-good; else baseline + quarantine
 *   - save: serialize → validate → atomic write → re-validate → promote last-good
 *
 * Invalid keys: ignored (soft warnings). Invalid structure: hard fail.
 * Unknown option keys applied later against XML catalog are skipped there too.
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

/** Where a successful load came from (boundary reporting). */
enum class ShellJsonLoadSource
{
    missing,     /* no file — not an error for first-run */
    primary,     /* config.json validated */
    last_good,   /* primary bad; last-good used */
    baseline,    /* all bad / missing last-good; baseline written */
};

struct ShellJsonValidateResult
{
    bool ok = false; /* hard structure ok (parseable object with schema shape) */
    std::vector<std::string> hard_errors;
    std::vector<std::string> soft_warnings; /* ignored keys / type skips */
    std::vector<std::string> ignored_keys;

    void hard(std::string m)
    {
        hard_errors.push_back(std::move(m));
        ok = false;
    }

    void soft(std::string m)
    {
        soft_warnings.push_back(std::move(m));
    }

    void ignore_key(std::string k)
    {
        ignored_keys.push_back(std::move(k));
    }

    std::string summary() const;
};

struct ShellJsonLoadResult
{
    bool ok = false;
    ShellJsonConfig cfg;
    ShellJsonLoadSource source = ShellJsonLoadSource::missing;
    std::string error;
    std::string quarantined_path; /* if primary was moved for inspection */
    ShellJsonValidateResult validation;
};

/** Default path ~/.config/wf-shell/config.json */
std::string shell_json_config_path();

/** Sibling paths for backup / quarantine (derived from primary). */
std::string shell_json_last_good_path(const std::string& primary = {});
std::string shell_json_quarantine_dir(const std::string& primary = {});

/**
 * Pure: validate JSON text against shell schema.
 * Hard fail = unusable document. Soft = ignore and continue.
 */
ShellJsonValidateResult validate_shell_json_text(const std::string& text);

/** Parse JSON text into ShellJsonConfig (pure). Calls validate first. */
bool parse_shell_json_config(const std::string& text, ShellJsonConfig& out,
    std::string *error = nullptr,
    ShellJsonValidateResult *validation = nullptr);

/** Serialize to pretty JSON string (pure). */
std::string serialize_shell_json_config(const ShellJsonConfig& cfg);

/** Pure baseline: version 1 + MCP stubs, empty panel/background/dock. */
ShellJsonConfig make_baseline_shell_json();

/**
 * Load with recovery:
 *   primary → last-good → write baseline (quarantine bad files).
 * missing primary without last-good → ok=false source=missing (first-run).
 */
ShellJsonLoadResult load_shell_json_config_resilient(const std::string& path);

/** Load from disk if present (strict primary only; no recovery). Prefer resilient. */
bool load_shell_json_config(const std::string& path, ShellJsonConfig& out,
    std::string *error = nullptr);

/**
 * Save: serialize → validate → write → re-validate → promote last-good.
 * Refuses to write if serialized form fails validation.
 */
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
 * Uses resilient load; validates before/after write.
 */
bool settings_save_section(const std::string& section,
    const std::map<std::string, std::string>& kv,
    std::string *error = nullptr);

/** Ensure default mcp stub exists when creating first JSON file. */
void ensure_default_mcp_stub(ShellJsonConfig& cfg);

/** Write baseline config to path (and last-good). Used when all configs invalid. */
bool write_baseline_shell_json(const std::string& path, std::string *error = nullptr);

} // namespace wf_shell
