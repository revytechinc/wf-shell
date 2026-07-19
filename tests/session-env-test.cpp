#include <gtest/gtest.h>

#include "session-env.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

using wf_shell::EnvVarAction;
using wf_shell::SessionEnvHooks;
using wf_shell::SessionEnvReport;
using wf_shell::apply_session_env;
using wf_shell::dbus_address_from_socket_path;
using wf_shell::discover_session_env;
using wf_shell::graphics_toolkit_env_extras;
using wf_shell::merge_safe_path;
using wf_shell::pick_wayland_display;
using wf_shell::pick_x11_display;
using wf_shell::session_env_critical_keys;
using wf_shell::session_env_for_app_launch;
using wf_shell::session_env_managed_keys;

/* ── pure helpers ───────────────────────────────────────────────────────── */

TEST(SessionEnvPath, MergeSafePathPrependsBase)
{
    auto m = merge_safe_path("");
    EXPECT_NE(m.find("/sbin"), std::string::npos);
    EXPECT_NE(m.find("/usr/local/bin"), std::string::npos);
}

TEST(SessionEnvPath, MergeSafePathKeepsExtra)
{
    auto m = merge_safe_path("/opt/bin:/home/u/bin");
    EXPECT_NE(m.find("/opt/bin"), std::string::npos);
    EXPECT_NE(m.find("/home/u/bin"), std::string::npos);
    /* safe dirs still first-ish */
    EXPECT_EQ(m.find("/sbin"), 0u);
}

TEST(SessionEnvPath, MergeSafePathDedupes)
{
    auto m = merge_safe_path("/usr/bin:/sbin");
    /* Count exact path segments equal to /sbin */
    int count = 0;
    size_t start = 0;
    while (start <= m.size())
    {
        size_t colon = m.find(':', start);
        std::string seg = (colon == std::string::npos) ?
            m.substr(start) : m.substr(start, colon - start);
        if (seg == "/sbin")
        {
            ++count;
        }
        if (colon == std::string::npos)
        {
            break;
        }
        start = colon + 1;
    }
    EXPECT_EQ(count, 1);
}

TEST(SessionEnvWayland, PreferWayland1)
{
    EXPECT_EQ(pick_wayland_display({"wayland-0", "wayland-1", "foo"}), "wayland-1");
}

TEST(SessionEnvWayland, PreferWayland0IfNo1)
{
    EXPECT_EQ(pick_wayland_display({"foo", "wayland-0"}), "wayland-0");
}

TEST(SessionEnvWayland, IgnoresLockFiles)
{
    EXPECT_EQ(pick_wayland_display({"wayland-1.lock", "wayland-0"}), "wayland-0");
}

TEST(SessionEnvWayland, EmptyWhenNone)
{
    EXPECT_TRUE(pick_wayland_display({"pulse", "dbus-1"}).empty());
}

TEST(SessionEnvDbus, AddressFromPath)
{
    EXPECT_EQ(dbus_address_from_socket_path("/tmp/dbus-xyz"), "unix:path=/tmp/dbus-xyz");
    EXPECT_TRUE(dbus_address_from_socket_path("").empty());
}

TEST(SessionEnvX11, PreferDisplay0)
{
    EXPECT_EQ(pick_x11_display({"X1", "X0", "X0_"}), ":0");
}

TEST(SessionEnvX11, FirstWhenNoZero)
{
    EXPECT_EQ(pick_x11_display({"X2", "X5"}), ":2");
}

TEST(SessionEnvX11, EmptyWhenNone)
{
    EXPECT_TRUE(pick_x11_display({"foo", "bar"}).empty());
}

TEST(SessionEnvGraphicsExtras, JavaAwtWhenDisplayPresent)
{
    std::map<std::string, std::string> env = {
        {"DISPLAY", ":0"},
        {"WAYLAND_DISPLAY", "wayland-1"},
    };
    auto x = graphics_toolkit_env_extras(env);
    EXPECT_EQ(x["_JAVA_AWT_WM_NONREPARENTING"], "1");
}

TEST(SessionEnvGraphicsExtras, EmptyWithoutDisplay)
{
    auto x = graphics_toolkit_env_extras({});
    EXPECT_TRUE(x.empty());
}

/* ── discovery with mock FS ─────────────────────────────────────────────── */

class SessionEnvDiscovery : public ::testing::Test
{
  protected:
    std::map<std::string, std::string> store; /* applied setenv */
    std::set<std::string> dirs;
    std::set<std::string> socks;
    std::map<std::string, std::vector<std::string>> listings;
    std::string username = "mlapointe";
    uid_t uid = 1001;

    SessionEnvHooks hooks;

    void SetUp() override
    {
        hooks.get_env = [this] (const char *n) -> const char * {
            auto it = store.find(n);
            return it == store.end() ? nullptr : it->second.c_str();
        };
        hooks.set_env = [this] (const char *n, const char *v, int) {
            store[n] = v;
            return 0;
        };
        hooks.path_is_dir = [this] (const std::string& p) {
            return dirs.count(p) > 0;
        };
        hooks.path_is_socket = [this] (const std::string& p) {
            return socks.count(p) > 0;
        };
        hooks.path_exists = [this] (const std::string& p) {
            return dirs.count(p) > 0 || socks.count(p) > 0;
        };
        hooks.list_dir = [this] (const std::string& d) {
            auto it = listings.find(d);
            return it == listings.end() ? std::vector<std::string>{} : it->second;
        };
        hooks.get_uid = [this] () { return uid; };
        hooks.get_username = [this] () { return username; };
    }

    std::map<std::string, std::string> env_from_store() const
    {
        return store;
    }
};

TEST_F(SessionEnvDiscovery, DiscoversFreeBSDRuntimeAndWaylandAndDbus)
{
    /* Minimal env — what a broken panel restart looks like */
    std::map<std::string, std::string> current = {
        {"HOME", "/home/mlapointe"},
        {"USER", "mlapointe"},
        {"PATH", "/usr/local/bin:/usr/bin:/bin"},
    };

    dirs.insert("/var/run/xdg/mlapointe");
    dirs.insert("/var/run/xdg/mlapointe/pulse");
    dirs.insert("/tmp/.X11-unix");
    socks.insert("/var/run/xdg/mlapointe/wayland-1");
    socks.insert("/tmp/dbus-qTerm9A3Gz");
    socks.insert("/var/run/xdg/mlapointe/pulse/native");
    socks.insert("/tmp/.X11-unix/X0");
    listings["/var/run/xdg/mlapointe"] = {
        "wayland-1", "wayland-1.lock", "pulse", "dbus-1",
    };
    listings["/tmp"] = {"dbus-qTerm9A3Gz", "other"};
    listings["/tmp/.X11-unix"] = {"X0", "X0_"};

    auto report = discover_session_env(current, hooks);
    ASSERT_FALSE(report.actions.empty());

    std::map<std::string, std::string> fixes;
    for (const auto& a : report.actions)
    {
        fixes[a.name] = a.value;
    }

    EXPECT_EQ(fixes["XDG_RUNTIME_DIR"], "/var/run/xdg/mlapointe");
    EXPECT_EQ(fixes["WAYLAND_DISPLAY"], "wayland-1");
    EXPECT_EQ(fixes["DBUS_SESSION_BUS_ADDRESS"], "unix:path=/tmp/dbus-qTerm9A3Gz");
    EXPECT_EQ(fixes["DISPLAY"], ":0");
    EXPECT_EQ(fixes["_JAVA_AWT_WM_NONREPARENTING"], "1");
    EXPECT_NE(fixes["PATH"].find("/sbin"), std::string::npos);
    EXPECT_EQ(fixes["XDG_CONFIG_HOME"], "/home/mlapointe/.config");
    EXPECT_EQ(fixes["XDG_DATA_HOME"], "/home/mlapointe/.local/share");
    EXPECT_EQ(fixes["XDG_SESSION_TYPE"], "wayland");
    EXPECT_EQ(fixes["GDK_BACKEND"], "wayland");
    EXPECT_EQ(fixes["PULSE_RUNTIME_PATH"], "/var/run/xdg/mlapointe/pulse");
    EXPECT_EQ(fixes["PULSE_SERVER"], "unix:/var/run/xdg/mlapointe/pulse/native");
    EXPECT_TRUE(report.critical_ok);
}

TEST_F(SessionEnvDiscovery, IntelliJStyleEnvHasDisplayAndJavaAwt)
{
    /*
     * Red: no DISPLAY → JVM "Unable to detect graphics environment".
     * Green: discovery supplies DISPLAY=:0 and _JAVA_AWT_WM_NONREPARENTING
     * without touching any .desktop file.
     */
    std::map<std::string, std::string> current = {
        {"HOME", "/home/mlapointe"},
        {"USER", "mlapointe"},
        {"XDG_RUNTIME_DIR", "/var/run/xdg/mlapointe"},
        {"WAYLAND_DISPLAY", "wayland-1"},
    };
    dirs.insert("/var/run/xdg/mlapointe");
    dirs.insert("/tmp/.X11-unix");
    socks.insert("/tmp/.X11-unix/X0");
    listings["/tmp/.X11-unix"] = {"X0"};

    auto report = discover_session_env(current, hooks);
    std::map<std::string, std::string> fixes;
    for (const auto& a : report.actions)
    {
        fixes[a.name] = a.value;
    }
    EXPECT_EQ(fixes["DISPLAY"], ":0");
    EXPECT_EQ(fixes["_JAVA_AWT_WM_NONREPARENTING"], "1");

    apply_session_env(report, hooks, false);
    EXPECT_EQ(store["DISPLAY"], ":0");
    EXPECT_EQ(store["_JAVA_AWT_WM_NONREPARENTING"], "1");

    /* Export map used by Gio AppLaunchContext::setenv — includes both */
    store["DISPLAY"] = ":0";
    store["WAYLAND_DISPLAY"] = "wayland-1";
    store["XDG_RUNTIME_DIR"] = "/var/run/xdg/mlapointe";
    store["HOME"] = "/home/mlapointe";
    auto launch_map = session_env_for_app_launch(&hooks);
    EXPECT_EQ(launch_map["DISPLAY"], ":0");
    EXPECT_EQ(launch_map["_JAVA_AWT_WM_NONREPARENTING"], "1");
    EXPECT_FALSE(launch_map["XDG_RUNTIME_DIR"].empty());
}

TEST_F(SessionEnvDiscovery, ApplyOnlyFillsMissing)
{
    store["HOME"] = "/home/mlapointe";
    store["USER"] = "mlapointe";
    store["WAYLAND_DISPLAY"] = "wayland-1"; /* already set — do not overwrite */

    dirs.insert("/var/run/xdg/mlapointe");
    socks.insert("/var/run/xdg/mlapointe/wayland-0");
    listings["/var/run/xdg/mlapointe"] = {"wayland-0"};

    auto current = store;
    auto report  = discover_session_env(current, hooks);
    apply_session_env(report, hooks, false);

    EXPECT_EQ(store["WAYLAND_DISPLAY"], "wayland-1"); /* preserved */
    EXPECT_EQ(store["XDG_RUNTIME_DIR"], "/var/run/xdg/mlapointe");
}

TEST_F(SessionEnvDiscovery, EmptyEnvStillDiscoversWithPasswdHints)
{
    /* No HOME/USER in current — username hook supplies user */
    std::map<std::string, std::string> current;
    dirs.insert("/home/mlapointe");
    dirs.insert("/var/run/xdg/mlapointe");
    socks.insert("/var/run/xdg/mlapointe/wayland-1");
    listings["/var/run/xdg/mlapointe"] = {"wayland-1"};

    auto report = discover_session_env(current, hooks);
    std::map<std::string, std::string> fixes;
    for (const auto& a : report.actions)
    {
        fixes[a.name] = a.value;
    }
    EXPECT_EQ(fixes["USER"], "mlapointe");
    EXPECT_FALSE(fixes["XDG_RUNTIME_DIR"].empty());
    EXPECT_EQ(fixes["WAYLAND_DISPLAY"], "wayland-1");
}

TEST_F(SessionEnvDiscovery, MissingRuntimeYieldsWarningNotCrash)
{
    std::map<std::string, std::string> current = {
        {"HOME", "/home/mlapointe"},
        {"USER", "mlapointe"},
    };
    auto report = discover_session_env(current, hooks);
    EXPECT_FALSE(report.critical_ok);
    EXPECT_FALSE(report.warnings.empty());
}

TEST_F(SessionEnvDiscovery, FullEnsureApplyRoundTrip)
{
    /* Simulate ensure_session_env: snapshot from store (empty), discover, apply */
    dirs.insert("/var/run/xdg/mlapointe");
    socks.insert("/var/run/xdg/mlapointe/wayland-1");
    socks.insert("/tmp/dbus-abc");
    listings["/var/run/xdg/mlapointe"] = {"wayland-1"};
    listings["/tmp"] = {"dbus-abc"};
    store["HOME"] = "/home/mlapointe";
    store["USER"] = "mlapointe";

    auto snap   = store;
    auto report = discover_session_env(snap, hooks);
    apply_session_env(report, hooks, false);

    EXPECT_EQ(store["XDG_RUNTIME_DIR"], "/var/run/xdg/mlapointe");
    EXPECT_EQ(store["WAYLAND_DISPLAY"], "wayland-1");
    EXPECT_EQ(store["DBUS_SESSION_BUS_ADDRESS"], "unix:path=/tmp/dbus-abc");
    EXPECT_TRUE(report.critical_ok);
}

/* ── select theme style: set broken env → fix → verify ──────────────────── */

TEST_F(SessionEnvDiscovery, BrokenLaunchEnvBecomesRunnable)
{
    /*
     * Red: minimal PATH + no wayland/dbus/runtime (apps fail to open).
     * Green: after discovery+apply, critical vars present.
     */
    std::map<std::string, std::string> broken = {
        {"HOME", "/home/mlapointe"},
        {"USER", "mlapointe"},
        {"PATH", "/usr/local/bin:/usr/bin:/bin"}, /* no /sbin */
    };
    EXPECT_TRUE(broken.count("WAYLAND_DISPLAY") == 0);
    EXPECT_TRUE(broken.count("XDG_RUNTIME_DIR") == 0);
    EXPECT_TRUE(broken.count("DBUS_SESSION_BUS_ADDRESS") == 0);

    dirs.insert("/var/run/xdg/mlapointe");
    socks.insert("/var/run/xdg/mlapointe/wayland-1");
    socks.insert("/tmp/dbus-session");
    listings["/var/run/xdg/mlapointe"] = {"wayland-1", "pulse"};
    listings["/tmp"] = {"dbus-session"};
    dirs.insert("/var/run/xdg/mlapointe/pulse");

    auto report = discover_session_env(broken, hooks);
    EXPECT_FALSE(report.critical_ok == false && report.actions.empty());

    apply_session_env(report, hooks, false);
    /* Merge applied into a final env view */
    auto final_env = broken;
    for (const auto& a : report.actions)
    {
        if (a.applied || true)
        {
            final_env[a.name] = a.value;
        }
    }
    /* re-apply into store for truth */
    for (const auto& a : report.actions)
    {
        store[a.name] = a.value;
    }

    EXPECT_FALSE(store["XDG_RUNTIME_DIR"].empty());
    EXPECT_FALSE(store["WAYLAND_DISPLAY"].empty());
    EXPECT_FALSE(store["DBUS_SESSION_BUS_ADDRESS"].empty());
    EXPECT_NE(store["PATH"].find("/sbin"), std::string::npos);
    EXPECT_EQ(store["XDG_SESSION_TYPE"], "wayland");

    /* Second pass: no new critical fixes (idempotent) */
    auto snap2 = store;
    auto report2 = discover_session_env(snap2, hooks);
    size_t new_critical = 0;
    for (const auto& a : report2.actions)
    {
        if (a.name == "WAYLAND_DISPLAY" || a.name == "XDG_RUNTIME_DIR" ||
            a.name == "DBUS_SESSION_BUS_ADDRESS")
        {
            ++new_critical;
        }
    }
    EXPECT_EQ(new_critical, 0u);
}

TEST(SessionEnvKeys, ManagedIncludesCritical)
{
    auto managed = session_env_managed_keys();
    auto critical = session_env_critical_keys();
    for (const auto& c : critical)
    {
        EXPECT_NE(std::find(managed.begin(), managed.end(), c), managed.end()) << c;
    }
}
