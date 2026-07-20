#include "user-config.hpp"

#include "shell-json-config.hpp"

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wf_shell
{
namespace
{

namespace fs = std::filesystem;

std::string errno_msg()
{
    return std::strerror(errno);
}

bool path_is_writable_file(const std::string& path, std::string *error)
{
    if (access(path.c_str(), W_OK) == 0)
    {
        return true;
    }
    /* try open append */
    std::ofstream o(path, std::ios::app);
    if (o)
    {
        return true;
    }
    if (error)
    {
        *error = "Cannot write config file \"" + path + "\": " + errno_msg();
    }
    return false;
}

} // namespace

bool ensure_parent_directories(const std::string& path, std::string *error)
{
    fs::path p(path);
    fs::path parent = p.parent_path();
    if (parent.empty() || parent == ".")
    {
        return true;
    }
    std::error_code ec;
    if (fs::is_directory(parent, ec))
    {
        return true;
    }
    if (!fs::create_directories(parent, ec) && !fs::is_directory(parent, ec))
    {
        if (error)
        {
            *error = "Cannot create config folder \"" + parent.string() + "\": " +
                (ec ? ec.message() : errno_msg());
        }
        return false;
    }
    return true;
}

UserConfigEnsure ensure_user_config_file(const std::string& path,
    const std::string& seed_path,
    const std::string& header_if_empty)
{
    UserConfigEnsure r;
    r.path = path;
    if (path.empty())
    {
        r.error = "Config path is empty (HOME / XDG_CONFIG_HOME not set).";
        return r;
    }

    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && parent != "." && !fs::is_directory(parent, ec))
    {
        if (!fs::create_directories(parent, ec) && !fs::is_directory(parent, ec))
        {
            r.error = "Cannot create config folder \"" + parent.string() + "\": " +
                (ec ? ec.message() : errno_msg());
            return r;
        }
        r.created_dir = true;
    }

    if (fs::exists(path, ec) && !ec)
    {
        if (!path_is_writable_file(path, &r.error))
        {
            return r;
        }
        r.ok = true;
        return r;
    }

    /* File missing — create from system seed or empty header */
    if (!seed_path.empty() && fs::is_regular_file(seed_path, ec))
    {
        fs::copy_file(seed_path, path, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            r.error = "Cannot copy system defaults from \"" + seed_path +
                "\" to \"" + path + "\": " + ec.message();
            return r;
        }
        r.created_file = true;
        r.seeded_from_system = true;
    } else
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            r.error = "Cannot create config file \"" + path + "\": " + errno_msg();
            return r;
        }
        if (!header_if_empty.empty())
        {
            out << header_if_empty;
            if (header_if_empty.back() != '\n')
            {
                out << '\n';
            }
        }
        if (!out)
        {
            r.error = "Cannot write new config file \"" + path + "\".";
            return r;
        }
        r.created_file = true;
    }

    if (!path_is_writable_file(path, &r.error))
    {
        return r;
    }
    r.ok = true;
    return r;
}

std::string system_wf_shell_ini_path()
{
    const char *cands[] = {
        "/usr/local/etc/wf-shell/wf-shell.ini",
        "/etc/wf-shell/wf-shell.ini",
    };
    for (const char *c : cands)
    {
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
        {
            return c;
        }
    }
    return {};
}

std::string system_wayfire_ini_path()
{
    /* FreeBSD ports PREFIX first; Linux FHS /etc/wayfire second */
    const char *cands[] = {
        "/usr/local/etc/wayfire/wayfire.ini",
        "/etc/wayfire/wayfire.ini",
    };
    for (const char *c : cands)
    {
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
        {
            return c;
        }
    }
    return {};
}

std::string user_wf_shell_ini_path()
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    if (const char *xh = std::getenv("XDG_CONFIG_HOME"); xh && xh[0])
    {
        return std::string(xh) + "/wf-shell.ini";
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

std::string user_wayfire_ini_path()
{
    if (const char *o = std::getenv("WAYFIRE_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    if (const char *xh = std::getenv("XDG_CONFIG_HOME"); xh && xh[0])
    {
        return std::string(xh) + "/wayfire.ini";
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wayfire.ini" : std::string{};
}

std::string user_shell_json_path()
{
    return shell_json_config_path();
}

bool ensure_settings_user_configs(std::string *error)
{
    /* JSON first — Settings primary store.
     * Use resilient path: if missing, write validated baseline (not raw {}). */
    {
        auto jpath = user_shell_json_path();
        if (jpath.empty())
        {
            if (error)
            {
                *error = "Cannot resolve config.json path (HOME unset).";
            }
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(jpath, ec))
        {
            if (!write_baseline_shell_json(jpath, error))
            {
                return false;
            }
        } else
        {
            /* Existing file: resilient load repairs invalid primary */
            auto lr = load_shell_json_config_resilient(jpath);
            if (!lr.ok)
            {
                if (error)
                {
                    *error = lr.error.empty()
                        ? "Could not load or repair config.json"
                        : lr.error;
                }
                return false;
            }
        }
    }
    /* Legacy dual-write INI — seed from package system default when first-run */
    {
        auto s = ensure_user_config_file(user_wf_shell_ini_path(),
            system_wf_shell_ini_path(),
            "# User wf-shell preferences (created by Settings)\n");
        if (!s.ok)
        {
            if (error)
            {
                *error = s.error;
            }
            return false;
        }
    }
    {
        auto w = ensure_user_config_file(user_wayfire_ini_path(),
            system_wayfire_ini_path(),
            "# User Wayfire preferences (created by Settings)\n");
        if (!w.ok)
        {
            if (error)
            {
                *error = w.error;
            }
            return false;
        }
    }
    return true;
}

} // namespace wf_shell
