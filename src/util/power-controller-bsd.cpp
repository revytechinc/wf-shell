/*
 * wf-shell power controller — BSD implementations (FreeBSD / OpenBSD / NetBSD).
 *
 * FreeBSD:   /sbin/shutdown, zzz / acpiconf
 * OpenBSD:   /sbin/shutdown, zzz
 * NetBSD:    /sbin/shutdown, zzz / rtcwake
 */

#include "power-controller.hpp"

#include <unistd.h>

#if defined(WFS_PLATFORM_FREEBSD)

#include <grp.h>
#include <sys/param.h>

#include <string>

/**
 * FreeBSD privilege model for power (no password prompts in the GUI path):
 *
 * - /sbin/shutdown is setuid root and group **operator** (mode 4550).
 *   Desktop users should be in operator — then shutdown/reboot work without doas.
 * - wheel alone does NOT execute /sbin/shutdown (permission denied).
 * - Suspend (zzz / acpiconf) often needs privilege; try bare command, then
 *   passwordless doas -n if configured.
 */
static bool in_named_group(const char *name)
{
    if (WFPowerController::is_root()) {
        return true;
    }
    struct group *gr = getgrnam(name);
    if (!gr) {
        return false;
    }
    const gid_t want = gr->gr_gid;

#ifdef NGROUPS_MAX
    gid_t groups[NGROUPS_MAX];
#else
    gid_t groups[64];
#endif
    int ngroups = getgroups(static_cast<int>(sizeof(groups) / sizeof(groups[0])), groups);
    if (ngroups < 0) {
        return false;
    }
    for (int i = 0; i < ngroups; i++) {
        if (groups[i] == want) {
            return true;
        }
    }
    /* also primary gid */
    if (getgid() == want) {
        return true;
    }
    return false;
}

static bool can_exec_path(const char *path)
{
    return path && access(path, X_OK) == 0;
}

/** Prefer unprivileged command; fall back to passwordless doas -n only. */
static std::string elevate_if_needed(const std::string& cmd)
{
    if (cmd.empty()) {
        return {};
    }
    if (WFPowerController::is_root()) {
        return cmd;
    }
    std::string first = cmd;
    const auto sp = first.find(' ');
    if (sp != std::string::npos) {
        first = first.substr(0, sp);
    }
    /* Absolute path: access(X_OK) knows setuid/group bits (e.g. operator+shutdown). */
    if (first.find('/') != std::string::npos) {
        if (can_exec_path(first.c_str())) {
            return cmd;
        }
    } else {
        /* PATH command — try bare first (zzz is often world-executable). */
        return cmd;
    }
    if (can_exec_path("/usr/local/bin/doas") || can_exec_path("/usr/bin/doas")) {
        return "doas -n " + cmd;
    }
    if (can_exec_path("/usr/local/bin/sudo") || can_exec_path("/usr/bin/sudo")) {
        return "sudo -n " + cmd;
    }
    return {}; /* not permitted without password hang */
}

class FreeBSDPowerController : public WFPowerController
{
  public:
    Capability query(Action action) override;
};

WFPowerController::Capability FreeBSDPowerController::query(Action action)
{
    Capability cap{.available = false, .permitted = false, .command = ""};

    switch (action) {
    case Action::Shutdown:
        cap.available = can_exec_path("/sbin/shutdown") || in_named_group("operator") ||
            in_named_group("wheel");
        /* operator can run setuid shutdown without doas */
        if (can_exec_path("/sbin/shutdown") || in_named_group("operator")) {
            cap.command = "/sbin/shutdown -p now";
            cap.permitted = true;
        } else if (in_named_group("wheel")) {
            cap.command = elevate_if_needed("/sbin/shutdown -p now");
            cap.permitted = !cap.command.empty();
        }
        break;

    case Action::Reboot:
        cap.available = can_exec_path("/sbin/shutdown") || in_named_group("operator") ||
            in_named_group("wheel");
        if (can_exec_path("/sbin/shutdown") || in_named_group("operator")) {
            cap.command = "/sbin/shutdown -r now";
            cap.permitted = true;
        } else if (in_named_group("wheel")) {
            cap.command = elevate_if_needed("/sbin/shutdown -r now");
            cap.permitted = !cap.command.empty();
        }
        break;

    case Action::Suspend:
        /* Prefer unprivileged; FreeBSD often requires elevation for acpiconf. */
        if (can_exec_path("/usr/sbin/zzz") || WFPowerController::check_permission("zzz")) {
            cap.available = true;
            cap.command = elevate_if_needed("zzz");
            cap.permitted = true;
        } else if (can_exec_path("/usr/sbin/acpiconf") ||
                   WFPowerController::check_permission("acpiconf")) {
            cap.available = true;
            cap.command = elevate_if_needed("acpiconf -s 3");
            cap.permitted = true;
        }
        break;

    case Action::Hibernate:
        /* FreeBSD does not support hibernation. */
        cap.available = false;
        cap.permitted = false;
        cap.command   = "";
        break;

    case Action::SwitchUser:
        /* No portable FreeBSD command for session switching.
         * Hide the button unless a display manager is detected. */
        cap.available = WFPowerController::check_permission("dm-tool list-seats") ||
                        WFPowerController::check_permission("gdm-control --list-sessions");
        if (cap.available) {
            cap.command = "dm-tool switch-to-greeter";
        }
        cap.permitted = cap.available;
        break;
    }

    return cap;
}

std::unique_ptr<WFPowerController> WFPowerController::create()
{
    return std::make_unique<FreeBSDPowerController>();
}

#elif defined(WFS_PLATFORM_OPENBSD)

class OpenBSDPowerController : public WFPowerController
{
  public:
    Capability query(Action action) override;
};

WFPowerController::Capability OpenBSDPowerController::query(Action action)
{
    Capability cap{.available = false, .permitted = false, .command = ""};

    switch (action) {
    case Action::Shutdown:
        cap.available = true;
        cap.command = "/sbin/shutdown -p now";
        cap.permitted = WFPowerController::is_root();
        break;

    case Action::Reboot:
        cap.available = true;
        cap.command = "/sbin/shutdown -r now";
        cap.permitted = WFPowerController::is_root();
        break;

    case Action::Suspend:
        /* OpenBSD uses zzz for suspend (aliased to apm -z). */
        cap.available = WFPowerController::check_permission("zzz");
        if (cap.available) {
            cap.command = "zzz";
        }
        cap.permitted = cap.available;
        break;

    case Action::Hibernate:
        cap.available = false;
        break;

    case Action::SwitchUser:
        /* No portable OpenBSD command. */
        cap.available = false;
        break;
    }

    return cap;
}

std::unique_ptr<WFPowerController> WFPowerController::create()
{
    return std::make_unique<OpenBSDPowerController>();
}

#elif defined(WFS_PLATFORM_NETBSD)

class NetBSDPowerController : public WFPowerController
{
  public:
    Capability query(Action action) override;
};

WFPowerController::Capability NetBSDPowerController::query(Action action)
{
    Capability cap{.available = false, .permitted = false, .command = ""};

    switch (action) {
    case Action::Shutdown:
        cap.available = true;
        cap.command = "/sbin/shutdown -p now";
        cap.permitted = WFPowerController::is_root();
        break;

    case Action::Reboot:
        cap.available = true;
        cap.command = "/sbin/shutdown -r now";
        cap.permitted = WFPowerController::is_root();
        break;

    case Action::Suspend:
        /* NetBSD uses rtcws to enter suspend, or zzz if available. */
        cap.available = WFPowerController::check_permission("zzz") ||
                        WFPowerController::check_permission("rtcwake");
        if (cap.available) {
            cap.command = WFPowerController::check_permission("zzz") ? "zzz" : "rtcwake -s mem";
        }
        cap.permitted = cap.available;
        break;

    case Action::Hibernate:
        /* NetBSD supports hibernation via suspend. */
        cap.available = WFPowerController::check_permission("zzz") ||
                        WFPowerController::check_permission("hibernate");
        if (cap.available) {
            cap.command = "zzz";
        }
        cap.permitted = cap.available;
        break;

    case Action::SwitchUser:
        cap.available = false;
        break;
    }

    return cap;
}

std::unique_ptr<WFPowerController> WFPowerController::create()
{
    return std::make_unique<NetBSDPowerController>();
}

#else

/* Stub for unknown platform — required so the translation unit compiles. */
std::unique_ptr<WFPowerController> WFPowerController::create()
{
    return nullptr;
}

#endif /* WFS_PLATFORM_* */
