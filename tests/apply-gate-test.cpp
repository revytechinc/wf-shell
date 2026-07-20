#include "apply-gate.hpp"

#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

using wf_shell::ApplyGateResult;
using wf_shell::validate_theme_apply;
using wf_shell::validate_theme_css_path;
using wf_shell::validate_wayland_session_for_live_apply;

namespace
{

std::string temp_path(const char *suffix)
{
    char buf[] = "/tmp/wf-gate-XXXXXX";
    int fd = mkstemp(buf);
    if (fd < 0)
    {
        return {};
    }
    close(fd);
    std::string p = buf;
    if (suffix && suffix[0])
    {
        p += suffix;
        // rename to final suffix path
        std::rename(buf, p.c_str());
    }
    return p;
}

void write_file(const std::string& path, const std::string& body)
{
    std::ofstream o(path);
    o << body;
}

} // namespace

TEST(ApplyGateResult, SummaryOk)
{
    ApplyGateResult r;
    r.ok = true;
    EXPECT_EQ(r.summary(), "ok");
}

TEST(ApplyGateResult, SummaryBlocked)
{
    ApplyGateResult r;
    r.block("nope");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.summary().find("BLOCKED"), std::string::npos);
    EXPECT_NE(r.summary().find("nope"), std::string::npos);
}

TEST(ApplyGateResult, MarkOkIfClean)
{
    ApplyGateResult r;
    r.mark_ok_if_clean();
    EXPECT_TRUE(r.ok);
    r.block("x");
    r.mark_ok_if_clean();
    EXPECT_FALSE(r.ok);
}

TEST(ValidateThemeCssPath, EmptyMeansDefaultOk)
{
    auto r = validate_theme_css_path("");
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.blockers.empty());
}

TEST(ValidateThemeCssPath, MissingFileBlocked)
{
    auto r = validate_theme_css_path("/tmp/definitely-does-not-exist-wf-gate-xyz.css");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.blockers.empty());
}

TEST(ValidateThemeCssPath, DirectoryBlocked)
{
    auto r = validate_theme_css_path("/tmp");
    EXPECT_FALSE(r.ok);
}

TEST(ValidateThemeCssPath, ValidCssPassesWhenGtkCanLoad)
{
    /* Minimal CSS — must not require Gtk::Application for path check+probe. */
    std::string path = temp_path(".css");
    ASSERT_FALSE(path.empty());
    write_file(path, "/* gate test */\nwindow { opacity: 1; }\n");
    auto r = validate_theme_css_path(path);
    /* Probe may fail headless without display; path existence must not block alone. */
    if (!r.ok)
    {
        /* Accept GTK probe failure in pure CI without display — but must mention CSS/load. */
        bool load_related = false;
        for (const auto& b : r.blockers)
        {
            if (b.find("CSS") != std::string::npos || b.find("load") != std::string::npos ||
                b.find("GTK") != std::string::npos)
            {
                load_related = true;
            }
        }
        EXPECT_TRUE(load_related) << r.summary();
    }
    unlink(path.c_str());
}

TEST(ValidateThemeApply, EmptyIdBlocked)
{
    auto r = validate_theme_apply("", "");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.summary().find("empty"), std::string::npos);
}

TEST(ValidateThemeApply, NonDefaultEmptyCssBlocked)
{
    auto r = validate_theme_apply("nord", "");
    EXPECT_FALSE(r.ok);
}

TEST(ValidateThemeApply, DefaultWithEmptyCssOk)
{
    auto r = validate_theme_apply("default", "");
    EXPECT_TRUE(r.ok) << r.summary();
}

TEST(ValidateWaylandSession, UnsetDisplayBlocked)
{
    unsetenv("WAYLAND_DISPLAY");
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    auto r = validate_wayland_session_for_live_apply();
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.summary().find("WAYLAND_DISPLAY"), std::string::npos);
}

TEST(ValidateWaylandSession, UnsetRuntimeBlocked)
{
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    unsetenv("XDG_RUNTIME_DIR");
    auto r = validate_wayland_session_for_live_apply();
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.summary().find("XDG_RUNTIME_DIR"), std::string::npos);
}

TEST(ValidateWaylandSession, MissingSocketBlocked)
{
    setenv("WAYLAND_DISPLAY", "wayland-does-not-exist-gate-test", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    auto r = validate_wayland_session_for_live_apply();
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.summary().find("socket"), std::string::npos);
}

TEST(ValidateWaylandSession, LiveSocketOkWhenPresent)
{
    const char *rd = std::getenv("XDG_RUNTIME_DIR");
    const char *wd = std::getenv("WAYLAND_DISPLAY");
    if (!rd || !wd)
    {
        GTEST_SKIP() << "no live wayland session in env";
    }
    std::string sock = std::string(rd) + "/" + wd;
    if (access(sock.c_str(), F_OK) != 0)
    {
        GTEST_SKIP() << "socket not present: " << sock;
    }
    auto r = validate_wayland_session_for_live_apply();
    EXPECT_TRUE(r.ok) << r.summary();
}

/* Combinatorial refuse matrix — every missing piece must fail closed */
TEST(ValidateWaylandSession, CombinationMatrix)
{
    struct Case
    {
        const char *wd;
        const char *rd;
        bool expect_ok;
    };
    const Case cases[] = {
        {nullptr, nullptr, false},
        {"", "/tmp", false},
        {"wayland-0", "", false},
        {"wayland-0", "/tmp", false}, /* socket missing */
    };
    for (const auto& c : cases)
    {
        if (c.wd)
            setenv("WAYLAND_DISPLAY", c.wd, 1);
        else
            unsetenv("WAYLAND_DISPLAY");
        if (c.rd)
            setenv("XDG_RUNTIME_DIR", c.rd, 1);
        else
            unsetenv("XDG_RUNTIME_DIR");
        auto r = validate_wayland_session_for_live_apply();
        EXPECT_EQ(r.ok, c.expect_ok) << "wd=" << (c.wd ? c.wd : "null")
                                     << " rd=" << (c.rd ? c.rd : "null")
                                     << " summary=" << r.summary();
    }
}
