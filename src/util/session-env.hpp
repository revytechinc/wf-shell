#pragma once

#include <functional>
#include <map>
#include <string>
#include <sys/types.h>
#include <vector>

/**
 * Session environment pre-flight for wf-shell.
 *
 * When the panel is started with a minimal environment (missing WAYLAND_DISPLAY,
 * XDG_RUNTIME_DIR, DBUS_SESSION_BUS_ADDRESS, PATH, …), apps launched from the
 * menu inherit that and fail. Discover correct values from the filesystem /
 * system and optionally setenv() them so children can run.
 *
 * Core discovery is pure (map in / report out) so unit tests can inject FS/env.
 */
namespace wf_shell
{

struct SessionEnvHooks
{
    /** getenv; return nullptr if unset */
    std::function<const char *(const char *name)> get_env;
    /** setenv(name, value, overwrite); return 0 on success */
    std::function<int(const char *name, const char *value, int overwrite)> set_env;
    std::function<bool(const std::string& path)> path_is_dir;
    std::function<bool(const std::string& path)> path_is_socket;
    std::function<bool(const std::string& path)> path_exists;
    /** Non-recursive directory basenames */
    std::function<std::vector<std::string>(const std::string& dir)> list_dir;
    std::function<uid_t()> get_uid;
    std::function<std::string()> get_username;
};

struct EnvVarAction
{
    std::string name;
    std::string value;
    std::string source;  /* e.g. "discovered:wayland-socket", "default:xdg-config" */
    bool was_missing = true;
    bool applied     = false;
};

struct SessionEnvReport
{
    std::vector<EnvVarAction> actions;
    std::vector<std::string> warnings;
    /** True if critical vars (HOME, XDG_RUNTIME_DIR, WAYLAND_DISPLAY) are usable */
    bool critical_ok = false;
};

/** Real OS hooks (getenv/setenv/stat/…). */
SessionEnvHooks default_session_env_hooks();

/**
 * Pure discovery: given current env snapshot + hooks for FS probes, compute
 * fixes. Does not call setenv.
 *
 * @param current  name → value (only set vars; missing keys are unset)
 */
SessionEnvReport discover_session_env(const std::map<std::string, std::string>& current,
    SessionEnvHooks& hooks);

/**
 * Apply report.actions via hooks.set_env (overwrite=1 for missing/empty only
 * unless force_overwrite is true).
 */
void apply_session_env(SessionEnvReport& report, SessionEnvHooks& hooks,
    bool force_overwrite = false);

/**
 * Full pre-flight: read live env, discover, apply. Safe to call often.
 * Logs to stderr when fixes are applied.
 */
SessionEnvReport ensure_session_env(bool apply = true, SessionEnvHooks *hooks = nullptr);

/** Snapshot of getenv for keys we care about (for tests / logging). */
std::map<std::string, std::string> snapshot_session_env(SessionEnvHooks& hooks);

/** Critical keys required for Wayland app launch. */
const std::vector<std::string>& session_env_critical_keys();

/** All keys we may discover/set. */
const std::vector<std::string>& session_env_managed_keys();

/**
 * FreeBSD-safe PATH: ensures base dirs (/sbin, /bin, /usr/sbin, …) are present.
 * Pure: returns merged path string.
 */
std::string merge_safe_path(const std::string& existing);

/**
 * Pick best wayland display name from socket basenames in a runtime dir listing.
 * Prefers wayland-1 then wayland-0 then any wayland-*.
 */
std::string pick_wayland_display(const std::vector<std::string>& basenames);

/**
 * Build DBUS_SESSION_BUS_ADDRESS from candidate unix socket paths (first usable).
 */
std::string dbus_address_from_socket_path(const std::string& socket_path);

/**
 * Pick DISPLAY value from X11 socket basenames under /tmp/.X11-unix
 * (e.g. "X0", "X1" → ":0", ":1"). Prefers :0.
 */
std::string pick_x11_display(const std::vector<std::string>& basenames);

/**
 * After ensure_session_env (or given a live snapshot), build the full map of
 * env vars that should be injected into child app launches (Gio AppLaunchContext).
 * Includes discovered fixes + recommended Java/AWT helpers for XWayland.
 * Does not edit any .desktop files.
 */
std::map<std::string, std::string> session_env_for_app_launch(
    SessionEnvHooks *hooks = nullptr);

/**
 * Recommended extras for GUI toolkits / JVM when a display is available.
 * Pure: based on whether DISPLAY / WAYLAND_DISPLAY are present in @env.
 */
std::map<std::string, std::string> graphics_toolkit_env_extras(
    const std::map<std::string, std::string>& env);

} // namespace wf_shell
