#include "session-env.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace wf_shell
{
namespace
{

bool str_empty(const std::map<std::string, std::string>& m, const std::string& k)
{
    auto it = m.find(k);
    return it == m.end() || it->second.empty();
}

std::string get_map(const std::map<std::string, std::string>& m, const std::string& k)
{
    auto it = m.find(k);
    return it == m.end() ? std::string{} : it->second;
}

void add_fix(SessionEnvReport& r, const std::string& name, const std::string& value,
    const std::string& source, bool was_missing = true)
{
    if (value.empty())
    {
        return;
    }
    EnvVarAction a;
    a.name        = name;
    a.value       = value;
    a.source      = source;
    a.was_missing = was_missing;
    r.actions.push_back(std::move(a));
}

bool looks_like_wayland_socket(const std::string& base)
{
    return base.rfind("wayland-", 0) == 0 && base.find(".lock") == std::string::npos;
}

} // namespace

const std::vector<std::string>& session_env_critical_keys()
{
    static const std::vector<std::string> k = {
        "HOME", "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY",
    };
    return k;
}

const std::vector<std::string>& session_env_managed_keys()
{
    static const std::vector<std::string> k = {
        "HOME",
        "USER",
        "LOGNAME",
        "XDG_RUNTIME_DIR",
        "WAYLAND_DISPLAY",
        "DBUS_SESSION_BUS_ADDRESS",
        "PATH",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_CACHE_HOME",
        "XDG_STATE_HOME",
        "XDG_SESSION_TYPE",
        "DISPLAY",
        "GDK_BACKEND",
        "PULSE_RUNTIME_PATH",
        "PULSE_SERVER",
    };
    return k;
}

std::string merge_safe_path(const std::string& existing)
{
    static const char *const required[] = {
        "/sbin",
        "/bin",
        "/usr/sbin",
        "/usr/bin",
        "/usr/local/sbin",
        "/usr/local/bin",
        nullptr,
    };

    std::vector<std::string> parts;
    auto push_unique = [&] (const std::string& p) {
        if (p.empty())
        {
            return;
        }
        for (auto& x : parts)
        {
            if (x == p)
            {
                return;
            }
        }
        parts.push_back(p);
    };

    for (int i = 0; required[i]; ++i)
    {
        push_unique(required[i]);
    }

    /* Keep existing entries after safe prefix */
    std::string cur = existing;
    size_t start = 0;
    while (start <= cur.size())
    {
        size_t colon = cur.find(':', start);
        std::string seg = (colon == std::string::npos) ?
            cur.substr(start) : cur.substr(start, colon - start);
        push_unique(seg);
        if (colon == std::string::npos)
        {
            break;
        }
        start = colon + 1;
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
        {
            out += ':';
        }
        out += parts[i];
    }
    return out;
}

std::string pick_wayland_display(const std::vector<std::string>& basenames)
{
    std::vector<std::string> found;
    for (const auto& b : basenames)
    {
        if (looks_like_wayland_socket(b))
        {
            found.push_back(b);
        }
    }
    if (found.empty())
    {
        return {};
    }
    auto has = [&] (const char *n) {
        return std::find(found.begin(), found.end(), n) != found.end();
    };
    if (has("wayland-1"))
    {
        return "wayland-1";
    }
    if (has("wayland-0"))
    {
        return "wayland-0";
    }
    std::sort(found.begin(), found.end());
    return found.front();
}

std::string dbus_address_from_socket_path(const std::string& socket_path)
{
    if (socket_path.empty())
    {
        return {};
    }
    return "unix:path=" + socket_path;
}

SessionEnvHooks default_session_env_hooks()
{
    SessionEnvHooks h;
    h.get_env = [] (const char *n) -> const char * {
        return std::getenv(n);
    };
    h.set_env = [] (const char *n, const char *v, int ov) {
        return ::setenv(n, v, ov);
    };
    h.path_is_dir = [] (const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    };
    h.path_is_socket = [] (const std::string& p) {
        struct stat st{};
        if (::stat(p.c_str(), &st) != 0)
        {
            return false;
        }
#ifdef S_IFSOCK
        return S_ISSOCK(st.st_mode);
#else
        return true; /* best-effort */
#endif
    };
    h.path_exists = [] (const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0;
    };
    h.list_dir = [] (const std::string& dir) {
        std::vector<std::string> out;
        DIR *d = ::opendir(dir.c_str());
        if (!d)
        {
            return out;
        }
        while (auto *ent = ::readdir(d))
        {
            if (ent->d_name[0] == '.' &&
                (ent->d_name[1] == '\0' ||
                    (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            {
                continue;
            }
            out.emplace_back(ent->d_name);
        }
        ::closedir(d);
        return out;
    };
    h.get_uid = [] () { return ::getuid(); };
    h.get_username = [] () {
        if (const char *u = std::getenv("USER"))
        {
            if (u[0])
            {
                return std::string(u);
            }
        }
        if (const char *u = std::getenv("LOGNAME"))
        {
            if (u[0])
            {
                return std::string(u);
            }
        }
        auto *pw = ::getpwuid(::getuid());
        if (pw && pw->pw_name)
        {
            return std::string(pw->pw_name);
        }
        return std::string{};
    };
    return h;
}

std::map<std::string, std::string> snapshot_session_env(SessionEnvHooks& hooks)
{
    std::map<std::string, std::string> m;
    for (const auto& k : session_env_managed_keys())
    {
        const char *v = hooks.get_env ? hooks.get_env(k.c_str()) : nullptr;
        if (v && v[0])
        {
            m[k] = v;
        }
    }
    return m;
}

SessionEnvReport discover_session_env(const std::map<std::string, std::string>& current,
    SessionEnvHooks& hooks)
{
    SessionEnvReport r;
    auto is_dir = [&] (const std::string& p) {
        return hooks.path_is_dir && hooks.path_is_dir(p);
    };
    auto is_sock = [&] (const std::string& p) {
        return hooks.path_is_socket && hooks.path_is_socket(p);
    };
    auto exists = [&] (const std::string& p) {
        return hooks.path_exists && hooks.path_exists(p);
    };
    auto list = [&] (const std::string& d) {
        return hooks.list_dir ? hooks.list_dir(d) : std::vector<std::string>{};
    };

    /* ── HOME / USER ──────────────────────────────────────────────────── */
    std::string home = get_map(current, "HOME");
    std::string user = get_map(current, "USER");
    if (user.empty() && hooks.get_username)
    {
        user = hooks.get_username();
        if (!user.empty())
        {
            add_fix(r, "USER", user, "discovered:username");
        }
    }
    if (str_empty(current, "LOGNAME") && !user.empty())
    {
        add_fix(r, "LOGNAME", user, "default:same-as-user");
    }
    if (home.empty())
    {
        auto *pw = ::getpwuid(hooks.get_uid ? hooks.get_uid() : 0);
        if (pw && pw->pw_dir)
        {
            home = pw->pw_dir;
            add_fix(r, "HOME", home, "discovered:passwd");
        } else if (!user.empty())
        {
            home = "/home/" + user;
            if (is_dir(home))
            {
                add_fix(r, "HOME", home, "discovered:home-dir");
            } else
            {
                r.warnings.push_back("HOME unset and could not be discovered");
            }
        }
    }
    /* Apply pending home/user into a working view for later discovery */
    auto view = current;
    for (const auto& a : r.actions)
    {
        view[a.name] = a.value;
    }
    home = get_map(view, "HOME");
    user = get_map(view, "USER");
    if (user.empty() && hooks.get_username)
    {
        user = hooks.get_username();
    }

    /* ── XDG_RUNTIME_DIR ──────────────────────────────────────────────── */
    std::string runtime = get_map(view, "XDG_RUNTIME_DIR");
    if (runtime.empty() || !is_dir(runtime))
    {
        std::vector<std::string> candidates;
        if (!user.empty())
        {
            candidates.push_back("/var/run/xdg/" + user); /* FreeBSD */
            candidates.push_back("/run/user/" + user);
        }
        if (hooks.get_uid)
        {
            candidates.push_back("/run/user/" + std::to_string(hooks.get_uid()));
            candidates.push_back("/var/run/user/" + std::to_string(hooks.get_uid()));
        }
        if (!home.empty())
        {
            candidates.push_back(home + "/.xdg");
        }
        std::string found;
        for (const auto& c : candidates)
        {
            if (is_dir(c))
            {
                found = c;
                break;
            }
        }
        if (!found.empty())
        {
            add_fix(r, "XDG_RUNTIME_DIR", found, "discovered:runtime-dir");
            runtime = found;
            view["XDG_RUNTIME_DIR"] = found;
        } else
        {
            r.warnings.push_back("XDG_RUNTIME_DIR missing; no usable runtime dir found");
        }
    }

    /* ── WAYLAND_DISPLAY ──────────────────────────────────────────────── */
    std::string wl = get_map(view, "WAYLAND_DISPLAY");
    if (wl.empty() && !runtime.empty())
    {
        auto names = list(runtime);
        auto pick  = pick_wayland_display(names);
        if (!pick.empty())
        {
            std::string sock = runtime + "/" + pick;
            if (is_sock(sock) || exists(sock))
            {
                add_fix(r, "WAYLAND_DISPLAY", pick, "discovered:wayland-socket");
                wl = pick;
                view["WAYLAND_DISPLAY"] = pick;
            }
        }
        if (wl.empty())
        {
            r.warnings.push_back("WAYLAND_DISPLAY missing; no wayland-* socket in runtime dir");
        }
    }

    /* ── DBUS_SESSION_BUS_ADDRESS ─────────────────────────────────────── */
    if (str_empty(view, "DBUS_SESSION_BUS_ADDRESS"))
    {
        std::vector<std::string> dbus_socks;
        if (!runtime.empty())
        {
            dbus_socks.push_back(runtime + "/bus");
            dbus_socks.push_back(runtime + "/dbus-1/bus");
        }
        /* FreeBSD session buses often live in /tmp/dbus-* */
        for (const auto& base : list("/tmp"))
        {
            if (base.rfind("dbus-", 0) == 0)
            {
                dbus_socks.push_back("/tmp/" + base);
            }
        }
        std::string chosen;
        for (const auto& s : dbus_socks)
        {
            if (is_sock(s) || exists(s))
            {
                chosen = s;
                break;
            }
        }
        if (!chosen.empty())
        {
            add_fix(r, "DBUS_SESSION_BUS_ADDRESS",
                dbus_address_from_socket_path(chosen),
                "discovered:dbus-socket");
            view["DBUS_SESSION_BUS_ADDRESS"] = dbus_address_from_socket_path(chosen);
        } else
        {
            r.warnings.push_back("DBUS_SESSION_BUS_ADDRESS missing; no session bus socket found");
        }
    }

    /* ── PATH ─────────────────────────────────────────────────────────── */
    {
        std::string path = get_map(view, "PATH");
        std::string merged = merge_safe_path(path);
        if (path != merged)
        {
            add_fix(r, "PATH", merged,
                path.empty() ? "default:safe-path" : "merged:safe-path",
                path.empty());
            view["PATH"] = merged;
        }
    }

    /* ── XDG user dirs ────────────────────────────────────────────────── */
    if (!home.empty())
    {
        if (str_empty(view, "XDG_CONFIG_HOME"))
        {
            add_fix(r, "XDG_CONFIG_HOME", home + "/.config", "default:xdg-config");
        }
        if (str_empty(view, "XDG_DATA_HOME"))
        {
            add_fix(r, "XDG_DATA_HOME", home + "/.local/share", "default:xdg-data");
        }
        if (str_empty(view, "XDG_CACHE_HOME"))
        {
            add_fix(r, "XDG_CACHE_HOME", home + "/.cache", "default:xdg-cache");
        }
        if (str_empty(view, "XDG_STATE_HOME"))
        {
            add_fix(r, "XDG_STATE_HOME", home + "/.local/state", "default:xdg-state");
        }
    }

    /* ── Session type / GDK ───────────────────────────────────────────── */
    if (!get_map(view, "WAYLAND_DISPLAY").empty() || !wl.empty())
    {
        if (str_empty(view, "XDG_SESSION_TYPE"))
        {
            add_fix(r, "XDG_SESSION_TYPE", "wayland", "default:wayland-session");
        }
        if (str_empty(view, "GDK_BACKEND"))
        {
            add_fix(r, "GDK_BACKEND", "wayland", "default:gdk-wayland");
        }
    }

    /* ── DISPLAY (XWayland) ───────────────────────────────────────────── */
    if (str_empty(view, "DISPLAY"))
    {
        for (const char *d : {":0", ":1"})
        {
            std::string sock = "/tmp/.X11-unix/X";
            sock += (d + 1); /* skip ':' */
            if (exists(sock) || is_sock(sock))
            {
                add_fix(r, "DISPLAY", d, "discovered:x11-socket");
                break;
            }
        }
    }

    /* ── PulseAudio ───────────────────────────────────────────────────── */
    if (!runtime.empty())
    {
        std::string pulse = runtime + "/pulse";
        if (is_dir(pulse) && str_empty(view, "PULSE_RUNTIME_PATH"))
        {
            add_fix(r, "PULSE_RUNTIME_PATH", pulse, "discovered:pulse-runtime");
        }
        std::string native = pulse + "/native";
        if ((is_sock(native) || exists(native)) && str_empty(view, "PULSE_SERVER"))
        {
            add_fix(r, "PULSE_SERVER", "unix:" + native, "discovered:pulse-server");
        }
    }

    /* ── Critical OK? ─────────────────────────────────────────────────── */
    auto has = [&] (const char *k) {
        for (const auto& a : r.actions)
        {
            if (a.name == k && !a.value.empty())
            {
                return true;
            }
        }
        return !str_empty(view, k);
    };
    /* Recompute view with all actions */
    for (const auto& a : r.actions)
    {
        view[a.name] = a.value;
    }
    r.critical_ok = !str_empty(view, "HOME") &&
        !str_empty(view, "XDG_RUNTIME_DIR") &&
        !str_empty(view, "WAYLAND_DISPLAY");

    (void)has;
    return r;
}

void apply_session_env(SessionEnvReport& report, SessionEnvHooks& hooks, bool force_overwrite)
{
    if (!hooks.set_env)
    {
        return;
    }
    for (auto& a : report.actions)
    {
        if (a.value.empty())
        {
            continue;
        }
        const char *cur = hooks.get_env ? hooks.get_env(a.name.c_str()) : nullptr;
        /* Skip if already exactly the discovered value (idempotent). */
        if (cur && a.value == cur && !force_overwrite)
        {
            a.applied = false;
            continue;
        }
        /*
         * By default only fill missing/empty, but always apply PATH merges and
         * any action discovery flagged as a correction (was_missing or PATH).
         */
        bool missing = !cur || !cur[0];
        if (!missing && !force_overwrite && a.name != "PATH" && a.was_missing)
        {
            /* was_missing true but somehow now set — still apply discovered value */
        }
        if (!missing && !force_overwrite && a.name != "PATH" && !a.was_missing)
        {
            continue;
        }
        if (hooks.set_env(a.name.c_str(), a.value.c_str(), 1) == 0)
        {
            a.applied = true;
        } else
        {
            report.warnings.push_back("setenv failed for " + a.name + ": " +
                std::strerror(errno));
        }
    }
}

SessionEnvReport ensure_session_env(bool apply, SessionEnvHooks *hooks_in)
{
    SessionEnvHooks local;
    SessionEnvHooks *hooks = hooks_in;
    if (!hooks)
    {
        local = default_session_env_hooks();
        hooks = &local;
    }

    auto snap = snapshot_session_env(*hooks);
    auto report = discover_session_env(snap, *hooks);
    if (apply)
    {
        apply_session_env(report, *hooks, false);
        for (const auto& a : report.actions)
        {
            if (a.applied)
            {
                std::cerr << "wf-shell: session-env set " << a.name << "=" << a.value
                          << " (" << a.source << ")\n";
            }
        }
        for (const auto& w : report.warnings)
        {
            std::cerr << "wf-shell: session-env warning: " << w << "\n";
        }
    }
    return report;
}

} // namespace wf_shell
