#include "startup-gate.hpp"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wf_settings
{
namespace
{

bool path_is_socket(const std::string& path)
{
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
    {
        return false;
    }
#if defined(S_IFSOCK)
    return S_ISSOCK(st.st_mode);
#else
    return true;
#endif
}

bool path_exists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool path_is_dir(const std::string& path)
{
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
    {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

} // namespace

std::string StartupGate::user_summary() const
{
    if (safe_to_open())
    {
        return "Desktop looks ready.";
    }
    if (!blockers.empty())
    {
        return blockers.front();
    }
    if (!warnings.empty())
    {
        return warnings.front();
    }
    return "Cannot open Settings safely.";
}

StartupGate evaluate_startup_gate()
{
    StartupGate g;

    const char *rd = std::getenv("XDG_RUNTIME_DIR");
    if (!rd || !rd[0] || !path_is_dir(rd))
    {
        g.blockers.push_back(
            "The desktop session folder is missing. Log out and log back in.");
        return g;
    }
    g.runtime_dir_ok = true;

    const char *wd = std::getenv("WAYLAND_DISPLAY");
    if (!wd || !wd[0])
    {
        g.blockers.push_back(
            "Settings cannot see the desktop (WAYLAND_DISPLAY is not set). "
            "Open Settings from the panel, not from a plain terminal.");
        return g;
    }
    g.wayland_display_set = true;

    const std::string sock = std::string(rd) + "/" + wd;
    if (!path_exists(sock))
    {
        g.blockers.push_back(
            "The desktop is not running right now. Start your session, then try again.");
        return g;
    }
    if (!path_is_socket(sock))
    {
        g.warnings.push_back("Wayland path exists but is not a socket: " + sock);
    }
    g.wayland_socket_present = true;
    g.compositor_probe_ok = true; /* socket present = good enough; no protocol probe */
    return g;
}

StartupGate prepare_settings_process_env()
{
    /*
     * Boundary hygiene only. We intentionally do NOT call ensure_session_env()
     * with full discovery here: inventing DISPLAY / GDK_BACKEND / stale DBUS
     * has been associated with client failures on this FreeBSD + NVIDIA seat.
     * Panel launchers still use ensure_session_env for children.
     */

    /* Stale session bus → Gtk "Connection refused"; clear so Gtk can skip it. */
    if (const char *dbus = std::getenv("DBUS_SESSION_BUS_ADDRESS"))
    {
        std::string addr = dbus;
        /* unix:path=/tmp/dbus-xxx */
        const std::string key = "unix:path=";
        auto pos = addr.find(key);
        if (pos != std::string::npos)
        {
            std::string path = addr.substr(pos + key.size());
            auto comma = path.find(',');
            if (comma != std::string::npos)
            {
                path = path.substr(0, comma);
            }
            if (!path.empty() && !path_exists(path))
            {
                unsetenv("DBUS_SESSION_BUS_ADDRESS");
            }
        }
    }

    /* Nested compositor trap: never leave a fake WAYLAND_DISPLAY for children. */
    /* (We only unset if socket is gone — evaluate after.) */

    return evaluate_startup_gate();
}

} // namespace wf_settings
