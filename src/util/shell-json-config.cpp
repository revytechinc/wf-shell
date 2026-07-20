#include "shell-json-config.hpp"

#include "ini-file.hpp"
#include "user-config.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <wayfire/nonstd/json.hpp>

namespace wf_shell
{
namespace
{

namespace fs = std::filesystem;

const char *const kKnownRootKeys[] = {
    "version", "panel", "background", "dock", "locker", "mcp",
};

const char *const kKnownSections[] = {
    "panel", "background", "dock", "locker",
};

bool is_known_root_key(const std::string& k)
{
    for (const char *n : kKnownRootKeys)
    {
        if (k == n)
        {
            return true;
        }
    }
    return false;
}

bool is_known_section(const std::string& k)
{
    for (const char *n : kKnownSections)
    {
        if (k == n)
        {
            return true;
        }
    }
    return false;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        return {};
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

bool write_file_atomic(const std::string& path, const std::string& contents,
    std::string *error)
{
    if (!ensure_parent_directories(path, error))
    {
        return false;
    }
    std::error_code ec;
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
        {
            if (error)
            {
                *error = "Cannot write temporary config \"" + tmp + "\".";
            }
            return false;
        }
        out << contents;
        if (!out)
        {
            if (error)
            {
                *error = "Failed while writing temporary config \"" + tmp + "\".";
            }
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec)
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            std::remove(tmp.c_str());
            if (error)
            {
                *error = "Cannot create config file \"" + path + "\": " + ec.message();
            }
            return false;
        }
        out << contents;
        std::remove(tmp.c_str());
        if (!out && error)
        {
            *error = "Failed while writing config file \"" + path + "\".";
        }
        return static_cast<bool>(out);
    }
    return true;
}

std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
          case '\\': o += "\\\\"; break;
          case '"':  o += "\\\""; break;
          case '\n': o += "\\n"; break;
          case '\r': o += "\\r"; break;
          case '\t': o += "\\t"; break;
          default:   o += c; break;
        }
    }
    return o;
}

std::string stamp_now()
{
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(ms);
}

bool file_exists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

/**
 * Move path to quarantine dir for inspection. Returns quarantine path or empty.
 */
std::string quarantine_file(const std::string& path, const std::string& reason)
{
    if (!file_exists(path))
    {
        return {};
    }
    std::string qdir = shell_json_quarantine_dir(path);
    std::error_code ec;
    fs::create_directories(qdir, ec);
    std::string base = fs::path(path).filename().string();
    std::string dest = qdir + "/" + base + ".bad-" + stamp_now();
    fs::rename(path, dest, ec);
    if (ec)
    {
        fs::copy_file(path, dest, fs::copy_options::overwrite_existing, ec);
        if (!ec)
        {
            fs::remove(path, ec);
        }
    }
    /* Side-car reason */
    if (file_exists(dest))
    {
        std::ofstream note(dest + ".reason.txt");
        if (note)
        {
            note << reason << "\n";
        }
        return dest;
    }
    return {};
}

bool promote_last_good(const std::string& primary, std::string *error)
{
    if (!file_exists(primary))
    {
        if (error)
        {
            *error = "cannot promote last-good: primary missing";
        }
        return false;
    }
    auto lg = shell_json_last_good_path(primary);
    if (!ensure_parent_directories(lg, error))
    {
        return false;
    }
    std::error_code ec;
    fs::copy_file(primary, lg, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (error)
        {
            *error = "cannot write last-good backup: " + ec.message();
        }
        return false;
    }
    return true;
}

/** Structural validation on already-parsed root object. */
void validate_root_object(const wf::json_t& root, ShellJsonValidateResult& r)
{
    r.ok = true;
    if (!root.is_object())
    {
        r.hard("root must be a JSON object");
        return;
    }

    for (const auto& key : root.get_member_names())
    {
        if (!is_known_root_key(key))
        {
            r.soft("ignored unknown root key: " + key);
            r.ignore_key(key);
            continue;
        }
        if (key == "version")
        {
            if (!root["version"].is_int() && !root["version"].is_int64() &&
                !root["version"].is_double())
            {
                r.soft("version is not a number — treating as 1");
            }
            continue;
        }
        if (is_known_section(key))
        {
            if (!root[key].is_object())
            {
                r.soft("section \"" + key + "\" is not an object — ignored");
                r.ignore_key(key);
            }
            continue;
        }
        if (key == "mcp")
        {
            if (!root["mcp"].is_object())
            {
                r.soft("mcp is not an object — ignored");
                r.ignore_key(key);
                continue;
            }
            auto mcp = root["mcp"];
            if (mcp.has_member("enabled") && !mcp["enabled"].is_bool())
            {
                r.soft("mcp.enabled is not bool — ignored");
            }
            if (mcp.has_member("servers") && !mcp["servers"].is_array())
            {
                r.soft("mcp.servers is not an array — ignored");
            }
            if (mcp.has_member("servers") && mcp["servers"].is_array())
            {
                auto arr = mcp["servers"];
                for (size_t i = 0; i < arr.size(); ++i)
                {
                    if (!arr[i].is_object())
                    {
                        r.soft("mcp.servers[" + std::to_string(i) +
                            "] is not an object — skipped");
                        continue;
                    }
                    auto s = arr[i];
                    /* Unknown keys inside a server object: soft ignore */
                    for (const auto& sk : s.get_member_names())
                    {
                        static const char *known[] = {
                            "id", "name", "enabled", "transport", "command",
                            "args", "env", "notes",
                        };
                        bool okk = false;
                        for (const char *n : known)
                        {
                            if (sk == n)
                            {
                                okk = true;
                                break;
                            }
                        }
                        if (!okk)
                        {
                            r.soft("mcp.servers[" + std::to_string(i) +
                                "] ignored key: " + sk);
                            r.ignore_key("mcp.servers." + sk);
                        }
                    }
                }
            }
        }
    }
}

} // namespace

std::string ShellJsonValidateResult::summary() const
{
    if (ok && soft_warnings.empty() && ignored_keys.empty())
    {
        return "ok";
    }
    std::ostringstream o;
    if (!ok)
    {
        o << "INVALID";
        for (const auto& e : hard_errors)
        {
            o << " | " << e;
        }
    } else
    {
        o << "ok-with-warnings";
    }
    for (const auto& w : soft_warnings)
    {
        o << " | warn: " << w;
    }
    return o.str();
}

std::string shell_json_config_path()
{
    if (const char *o = std::getenv("WF_SHELL_JSON_CONFIG"); o && o[0])
    {
        return o;
    }
    const char *xh = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xh && xh[0])
    {
        base = xh;
    } else
    {
        const char *home = std::getenv("HOME");
        base = home ? std::string(home) + "/.config" : "/tmp";
    }
    return base + "/wf-shell/config.json";
}

std::string shell_json_last_good_path(const std::string& primary)
{
    std::string p = primary.empty() ? shell_json_config_path() : primary;
    return p + ".last-good";
}

std::string shell_json_quarantine_dir(const std::string& primary)
{
    std::string p = primary.empty() ? shell_json_config_path() : primary;
    return (fs::path(p).parent_path() / "quarantine").string();
}

ShellJsonValidateResult validate_shell_json_text(const std::string& text)
{
    ShellJsonValidateResult r;
    if (text.empty())
    {
        r.hard("empty json");
        return r;
    }
    wf::json_t root;
    auto err = wf::json_t::parse_string(text, root);
    if (err)
    {
        r.hard("json parse error: " + *err);
        return r;
    }
    validate_root_object(root, r);
    return r;
}

void ensure_default_mcp_stub(ShellJsonConfig& cfg)
{
    if (!cfg.mcp_servers.empty())
    {
        return;
    }
    McpServerConfig honcho;
    honcho.id = "honcho";
    honcho.name = "Honcho";
    honcho.enabled = false;
    honcho.transport = "stdio";
    honcho.command = "honcho";
    honcho.notes = "AI peer memory (enable when MCP runtime is integrated)";
    cfg.mcp_servers.push_back(honcho);

    McpServerConfig local;
    local.id = "desktop";
    local.name = "Desktop tools";
    local.enabled = false;
    local.transport = "stdio";
    local.command = "wf-mcp-desktop";
    local.notes = "Future: display, theme, session tools for agents";
    cfg.mcp_servers.push_back(local);
}

ShellJsonConfig make_baseline_shell_json()
{
    ShellJsonConfig cfg;
    cfg.version = 1;
    cfg.mcp_enabled = false;
    ensure_default_mcp_stub(cfg);
    /* Minimal known-good panel defaults (match package system where possible). */
    cfg.sections["panel"]["position"] = "top";
    cfg.sections["panel"]["layer"] = "top";
    cfg.sections["dock"]["position"] = "bottom";
    return cfg;
}

bool parse_shell_json_config(const std::string& text, ShellJsonConfig& out,
    std::string *error, ShellJsonValidateResult *validation)
{
    out = ShellJsonConfig{};
    auto vr = validate_shell_json_text(text);
    if (validation)
    {
        *validation = vr;
    }
    if (!vr.ok)
    {
        if (error)
        {
            *error = vr.hard_errors.empty() ? "invalid json" : vr.hard_errors.front();
        }
        return false;
    }

    wf::json_t root;
    auto err = wf::json_t::parse_string(text, root);
    if (err)
    {
        if (error)
        {
            *error = *err;
        }
        return false;
    }

    if (root.has_member("version") &&
        (root["version"].is_int() || root["version"].is_int64()))
    {
        out.version = root["version"].is_int()
            ? root["version"].as_int()
            : static_cast<int>(root["version"].as_int64());
    }

    auto load_section = [&] (const char *name) {
        if (!root.has_member(name) || !root[name].is_object())
        {
            return;
        }
        auto sec = root[name];
        for (const auto& key : sec.get_member_names())
        {
            auto v = sec[key];
            if (v.is_string())
            {
                out.sections[name][key] = v.as_string();
            } else if (v.is_bool())
            {
                out.sections[name][key] = v.as_bool() ? "true" : "false";
            } else if (v.is_int())
            {
                out.sections[name][key] = std::to_string(v.as_int());
            } else if (v.is_double())
            {
                out.sections[name][key] = std::to_string(v.as_double());
            } else if (v.is_int64())
            {
                out.sections[name][key] = std::to_string(
                    static_cast<long long>(v.as_int64()));
            }
            /* else: non-scalar ignored (already soft-warned at validate if needed) */
        }
    };
    load_section("panel");
    load_section("background");
    load_section("dock");
    load_section("locker");

    if (root.has_member("mcp") && root["mcp"].is_object())
    {
        auto mcp = root["mcp"];
        if (mcp.has_member("enabled") && mcp["enabled"].is_bool())
        {
            out.mcp_enabled = mcp["enabled"].as_bool();
        }
        if (mcp.has_member("servers") && mcp["servers"].is_array())
        {
            auto arr = mcp["servers"];
            for (size_t i = 0; i < arr.size(); ++i)
            {
                auto s = arr[i];
                if (!s.is_object())
                {
                    continue;
                }
                McpServerConfig sc;
                if (s.has_member("id") && s["id"].is_string())
                {
                    sc.id = s["id"].as_string();
                }
                if (s.has_member("name") && s["name"].is_string())
                {
                    sc.name = s["name"].as_string();
                }
                if (s.has_member("enabled") && s["enabled"].is_bool())
                {
                    sc.enabled = s["enabled"].as_bool();
                }
                if (s.has_member("transport") && s["transport"].is_string())
                {
                    sc.transport = s["transport"].as_string();
                }
                if (s.has_member("command") && s["command"].is_string())
                {
                    sc.command = s["command"].as_string();
                }
                if (s.has_member("notes") && s["notes"].is_string())
                {
                    sc.notes = s["notes"].as_string();
                }
                if (s.has_member("args") && s["args"].is_array())
                {
                    auto a = s["args"];
                    for (size_t j = 0; j < a.size(); ++j)
                    {
                        if (a[j].is_string())
                        {
                            sc.args.push_back(a[j].as_string());
                        }
                    }
                }
                if (s.has_member("env") && s["env"].is_object())
                {
                    auto e = s["env"];
                    for (const auto& k : e.get_member_names())
                    {
                        if (e[k].is_string())
                        {
                            sc.env[k] = e[k].as_string();
                        }
                    }
                }
                if (!sc.id.empty())
                {
                    out.mcp_servers.push_back(std::move(sc));
                }
            }
        }
    }
    return true;
}

std::string serialize_shell_json_config(const ShellJsonConfig& cfg)
{
    std::ostringstream o;
    o << "{\n  \"version\": " << cfg.version << ",\n";

    auto emit_section = [&] (const std::string& name, bool& first_sec) {
        auto it = cfg.sections.find(name);
        if (it == cfg.sections.end() || it->second.empty())
        {
            return;
        }
        if (!first_sec)
        {
            o << ",\n";
        }
        first_sec = false;
        o << "  \"" << name << "\": {\n";
        bool first = true;
        for (const auto& [k, v] : it->second)
        {
            if (!first)
            {
                o << ",\n";
            }
            first = false;
            o << "    \"" << json_escape(k) << "\": \"" << json_escape(v) << "\"";
        }
        o << "\n  }";
    };

    bool first_sec = true;
    emit_section("panel", first_sec);
    emit_section("background", first_sec);
    emit_section("dock", first_sec);
    emit_section("locker", first_sec);

    if (!first_sec)
    {
        o << ",\n";
    }
    o << "  \"mcp\": {\n";
    o << "    \"enabled\": " << (cfg.mcp_enabled ? "true" : "false") << ",\n";
    o << "    \"servers\": [\n";
    for (size_t i = 0; i < cfg.mcp_servers.size(); ++i)
    {
        const auto& s = cfg.mcp_servers[i];
        o << "      {\n";
        o << "        \"id\": \"" << json_escape(s.id) << "\",\n";
        o << "        \"name\": \"" << json_escape(s.name) << "\",\n";
        o << "        \"enabled\": " << (s.enabled ? "true" : "false") << ",\n";
        o << "        \"transport\": \"" << json_escape(s.transport) << "\",\n";
        o << "        \"command\": \"" << json_escape(s.command) << "\",\n";
        o << "        \"args\": [";
        for (size_t j = 0; j < s.args.size(); ++j)
        {
            if (j)
            {
                o << ", ";
            }
            o << "\"" << json_escape(s.args[j]) << "\"";
        }
        o << "],\n";
        o << "        \"env\": {";
        bool ef = true;
        for (const auto& [ek, ev] : s.env)
        {
            if (!ef)
            {
                o << ", ";
            }
            ef = false;
            o << "\"" << json_escape(ek) << "\": \"" << json_escape(ev) << "\"";
        }
        o << "},\n";
        o << "        \"notes\": \"" << json_escape(s.notes) << "\"\n";
        o << "      }";
        if (i + 1 < cfg.mcp_servers.size())
        {
            o << ",";
        }
        o << "\n";
    }
    o << "    ]\n";
    o << "  }\n";
    o << "}\n";
    return o.str();
}

bool write_baseline_shell_json(const std::string& path, std::string *error)
{
    auto cfg = make_baseline_shell_json();
    cfg.path = path;
    return save_shell_json_config(path, cfg, error);
}

bool load_shell_json_config(const std::string& path, ShellJsonConfig& out,
    std::string *error)
{
    auto text = read_file(path);
    if (text.empty())
    {
        if (error)
        {
            *error = "missing or empty: " + path;
        }
        return false;
    }
    ShellJsonValidateResult vr;
    if (!parse_shell_json_config(text, out, error, &vr))
    {
        return false;
    }
    out.loaded_from_disk = true;
    out.path = path;
    return true;
}

ShellJsonLoadResult load_shell_json_config_resilient(const std::string& path)
{
    ShellJsonLoadResult r;
    r.cfg.path = path;

    auto try_path = [&] (const std::string& p, ShellJsonLoadSource src) -> bool {
        if (!file_exists(p))
        {
            return false;
        }
        auto text = read_file(p);
        ShellJsonValidateResult vr;
        std::string err;
        ShellJsonConfig cfg;
        if (!parse_shell_json_config(text, cfg, &err, &vr))
        {
            r.validation = vr;
            r.error = err;
            return false;
        }
        /* Round-trip validate serialize (catches internal inconsistency) */
        auto ser = serialize_shell_json_config(cfg);
        auto vr2 = validate_shell_json_text(ser);
        if (!vr2.ok)
        {
            r.validation = vr2;
            r.error = "round-trip validation failed after parse";
            return false;
        }
        cfg.loaded_from_disk = true;
        cfg.path = path; /* logical primary path */
        r.cfg = std::move(cfg);
        r.validation = vr;
        r.source = src;
        r.ok = true;
        return true;
    };

    if (!file_exists(path))
    {
        /* First-run: optional last-good */
        if (try_path(shell_json_last_good_path(path), ShellJsonLoadSource::last_good))
        {
            return r;
        }
        r.source = ShellJsonLoadSource::missing;
        r.error = "missing: " + path;
        r.ok = false;
        return r;
    }

    if (try_path(path, ShellJsonLoadSource::primary))
    {
        return r;
    }

    /* Primary invalid — quarantine for inspection */
    std::string reason = r.error.empty() ? r.validation.summary() : r.error;
    r.quarantined_path = quarantine_file(path,
        "primary config failed validation: " + reason);
    std::cerr << "wf-shell:json: quarantined invalid config → "
              << (r.quarantined_path.empty() ? "(failed)" : r.quarantined_path)
              << " (" << reason << ")\n";

    if (try_path(shell_json_last_good_path(path), ShellJsonLoadSource::last_good))
    {
        std::cerr << "wf-shell:json: restored from last-good backup\n";
        /* Re-materialize primary from last-good so tools see a file */
        std::string werr;
        if (!write_file_atomic(path, serialize_shell_json_config(r.cfg), &werr))
        {
            std::cerr << "wf-shell:json: could not rewrite primary from last-good: "
                      << werr << "\n";
        }
        return r;
    }

    /* Quarantine last-good if it exists but is also bad */
    auto lg = shell_json_last_good_path(path);
    if (file_exists(lg))
    {
        auto ql = quarantine_file(lg, "last-good also failed validation");
        if (!ql.empty())
        {
            std::cerr << "wf-shell:json: quarantined last-good → " << ql << "\n";
        }
    }

    /* Baseline */
    r.cfg = make_baseline_shell_json();
    r.cfg.path = path;
    r.cfg.loaded_from_disk = true;
    std::string berr;
    if (!save_shell_json_config(path, r.cfg, &berr))
    {
        r.ok = false;
        r.source = ShellJsonLoadSource::baseline;
        r.error = "all configs invalid and baseline write failed: " + berr;
        return r;
    }
    r.source = ShellJsonLoadSource::baseline;
    r.ok = true;
    r.error = "primary and last-good invalid; wrote baseline default";
    std::cerr << "wf-shell:json: wrote baseline default config to " << path << "\n";
    return r;
}

bool save_shell_json_config(const std::string& path, const ShellJsonConfig& cfg,
    std::string *error)
{
    auto text = serialize_shell_json_config(cfg);

    /* Validate before write */
    auto vr = validate_shell_json_text(text);
    if (!vr.ok)
    {
        if (error)
        {
            *error = "refusing to write invalid JSON: " + vr.summary();
        }
        return false;
    }

    std::string werr;
    if (!write_file_atomic(path, text, &werr))
    {
        if (error)
        {
            *error = werr.empty() ? ("Could not save settings to \"" + path + "\".") : werr;
        }
        return false;
    }

    /* Validate after write (re-read) */
    auto disk = read_file(path);
    auto vr2 = validate_shell_json_text(disk);
    if (!vr2.ok)
    {
        /* Corrupt write — try restore last-good */
        auto lg = shell_json_last_good_path(path);
        if (file_exists(lg))
        {
            std::error_code ec;
            fs::copy_file(lg, path, fs::copy_options::overwrite_existing, ec);
        }
        if (error)
        {
            *error = "post-write validation failed; restored last-good if available: " +
                vr2.summary();
        }
        return false;
    }

    /* Promote last working backup */
    std::string perr;
    if (!promote_last_good(path, &perr))
    {
        /* Soft: save succeeded; backup optional */
        std::cerr << "wf-shell:json: last-good promote: " << perr << "\n";
    }
    return true;
}

void apply_shell_json_to_config_manager(const ShellJsonConfig& cfg,
    wf::config::config_manager_t& manager,
    std::vector<std::string> *warnings)
{
    for (const auto& [section, keys] : cfg.sections)
    {
        for (const auto& [key, value] : keys)
        {
            auto full = section + "/" + key;
            auto opt  = manager.get_option(full);
            if (!opt)
            {
                if (warnings)
                {
                    warnings->push_back("json override unknown option: " + full);
                }
                continue;
            }
            if (!opt->set_value_str(value) && warnings)
            {
                warnings->push_back("json override rejected: " + full + "=" + value);
            }
        }
    }
}

void shell_json_set(ShellJsonConfig& cfg, const std::string& section,
    const std::string& key, const std::string& value)
{
    cfg.sections[section][key] = value;
}

std::string shell_json_get(const ShellJsonConfig& cfg, const std::string& section,
    const std::string& key, const std::string& default_val)
{
    auto sit = cfg.sections.find(section);
    if (sit == cfg.sections.end())
    {
        return default_val;
    }
    auto kit = sit->second.find(key);
    if (kit == sit->second.end())
    {
        return default_val;
    }
    return kit->second;
}

bool settings_save_section(const std::string& section,
    const std::map<std::string, std::string>& kv,
    std::string *error)
{
    std::string ensure_err;
    if (!ensure_settings_user_configs(&ensure_err))
    {
        if (error)
        {
            *error = ensure_err.empty()
                ? "Could not create your settings files."
                : ensure_err;
        }
        return false;
    }

    auto path = shell_json_config_path();
    ShellJsonConfig cfg;
    auto loaded = load_shell_json_config_resilient(path);
    if (loaded.ok)
    {
        cfg = std::move(loaded.cfg);
        if (loaded.source == ShellJsonLoadSource::baseline ||
            loaded.source == ShellJsonLoadSource::last_good)
        {
            std::cerr << "wf-shell:settings: loaded JSON from "
                      << (loaded.source == ShellJsonLoadSource::baseline
                          ? "baseline" : "last-good")
                      << "\n";
        }
    } else
    {
        cfg = make_baseline_shell_json();
        ensure_default_mcp_stub(cfg);
    }
    for (const auto& [k, v] : kv)
    {
        shell_json_set(cfg, section, k, v);
    }
    cfg.path = path;
    if (!save_shell_json_config(path, cfg, error))
    {
        return false;
    }

    std::string ini = user_wf_shell_ini_path();
    if (ini.empty())
    {
        if (error)
        {
            *error = "HOME is not set; cannot write ~/.config/wf-shell.ini.";
        }
        return false;
    }
    {
        auto ens = ensure_user_config_file(ini, system_wf_shell_ini_path(),
            "# User wf-shell preferences (created by Settings)\n");
        if (!ens.ok)
        {
            if (error)
            {
                *error = ens.error;
            }
            return false;
        }
    }
    if (!ini_set_many(ini, section, kv))
    {
        if (error)
        {
            *error = "Saved preferences to JSON, but could not update \"" + ini +
                "\". Check that the file and its folder are writable.";
        }
        return false;
    }
    return true;
}

} // namespace wf_shell
