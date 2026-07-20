#include "shell-json-config.hpp"

#include "ini-file.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <wayfire/nonstd/json.hpp>

namespace wf_shell
{
namespace
{

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

bool write_file_atomic(const std::string& path, const std::string& contents)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << contents;
        if (!out)
        {
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec)
    {
        /* FreeBSD may fail rename across; try copy */
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << contents;
        std::remove(tmp.c_str());
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

} // namespace

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

void ensure_default_mcp_stub(ShellJsonConfig& cfg)
{
    if (!cfg.mcp_servers.empty())
    {
        return;
    }
    /* Scaffold for future AI tooling — disabled until wired. */
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

bool parse_shell_json_config(const std::string& text, ShellJsonConfig& out,
    std::string *error)
{
    out = ShellJsonConfig{};
    if (text.empty())
    {
        if (error)
        {
            *error = "empty json";
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
    if (!root.is_object())
    {
        if (error)
        {
            *error = "root must be object";
        }
        return false;
    }
    if (root.has_member("version") && root["version"].is_int())
    {
        out.version = root["version"].as_int();
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
                out.sections[name][key] = std::to_string(static_cast<long long>(v.as_int64()));
            }
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
    if (!parse_shell_json_config(text, out, error))
    {
        return false;
    }
    out.loaded_from_disk = true;
    out.path = path;
    return true;
}

bool save_shell_json_config(const std::string& path, const ShellJsonConfig& cfg,
    std::string *error)
{
    auto text = serialize_shell_json_config(cfg);
    if (!write_file_atomic(path, text))
    {
        if (error)
        {
            *error = "write failed: " + path;
        }
        return false;
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
    auto path = shell_json_config_path();
    ShellJsonConfig cfg;
    std::string lerr;
    if (!load_shell_json_config(path, cfg, &lerr))
    {
        cfg = ShellJsonConfig{};
        cfg.version = 1;
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

    /* Dual-write legacy INI until fully retired */
    const char *home = std::getenv("HOME");
    std::string ini;
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        ini = o;
    } else if (home)
    {
        ini = std::string(home) + "/.config/wf-shell.ini";
    }
    if (!ini.empty())
    {
        (void)ini_set_many(ini, section, kv);
    }
    return true;
}

} // namespace wf_shell
