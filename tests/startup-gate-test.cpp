#include "startup-gate.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

using wf_settings::evaluate_startup_gate;
using wf_settings::prepare_settings_process_env;
using wf_settings::StartupGate;

TEST(StartupGate, UnsafeWithoutRuntimeDir)
{
    unsetenv("XDG_RUNTIME_DIR");
    unsetenv("WAYLAND_DISPLAY");
    auto g = evaluate_startup_gate();
    EXPECT_FALSE(g.safe_to_open());
    EXPECT_FALSE(g.blockers.empty());
    EXPECT_FALSE(g.user_summary().empty());
}

TEST(StartupGate, UnsafeWithoutWaylandDisplay)
{
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    unsetenv("WAYLAND_DISPLAY");
    auto g = evaluate_startup_gate();
    EXPECT_FALSE(g.safe_to_open());
    EXPECT_TRUE(g.runtime_dir_ok);
    EXPECT_FALSE(g.wayland_display_set);
}

TEST(StartupGate, UnsafeWhenSocketMissing)
{
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    setenv("WAYLAND_DISPLAY", "wayland-missing-startup-gate", 1);
    auto g = evaluate_startup_gate();
    EXPECT_FALSE(g.safe_to_open());
    EXPECT_TRUE(g.wayland_display_set);
    EXPECT_FALSE(g.wayland_socket_present);
}

TEST(StartupGate, SafeWhenLiveSessionPresent)
{
    const char *rd = std::getenv("XDG_RUNTIME_DIR");
    const char *wd = std::getenv("WAYLAND_DISPLAY");
    /* Parent may have unset these in prior tests — re-read from process if live. */
    if (!rd || !rd[0] || !wd || !wd[0])
    {
        /* Try common FreeBSD session */
        const char *user = std::getenv("USER");
        if (user)
        {
            std::string cand = std::string("/var/run/xdg/") + user;
            if (access(cand.c_str(), X_OK) == 0)
            {
                setenv("XDG_RUNTIME_DIR", cand.c_str(), 1);
                rd = std::getenv("XDG_RUNTIME_DIR");
            }
        }
    }
    rd = std::getenv("XDG_RUNTIME_DIR");
    wd = std::getenv("WAYLAND_DISPLAY");
    if (!rd || !wd)
    {
        GTEST_SKIP() << "no live wayland env";
    }
    std::string sock = std::string(rd) + "/" + wd;
    if (access(sock.c_str(), F_OK) != 0)
    {
        GTEST_SKIP() << "no socket " << sock;
    }
    auto g = evaluate_startup_gate();
    EXPECT_TRUE(g.safe_to_open()) << g.user_summary();
    EXPECT_EQ(g.user_summary(), "Desktop looks ready.");
}

TEST(StartupGate, PrepareClearsStaleDbus)
{
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/definitely-no-dbus-socket-wf", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    unsetenv("WAYLAND_DISPLAY");
    (void)prepare_settings_process_env();
    const char *dbus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    EXPECT_TRUE(dbus == nullptr || dbus[0] == '\0')
        << "stale DBUS should be cleared, got: " << (dbus ? dbus : "(null)");
}

TEST(StartupGate, PrepareDoesNotInventWayland)
{
    unsetenv("WAYLAND_DISPLAY");
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    auto g = prepare_settings_process_env();
    EXPECT_FALSE(g.safe_to_open());
    EXPECT_EQ(std::getenv("WAYLAND_DISPLAY"), nullptr);
}
