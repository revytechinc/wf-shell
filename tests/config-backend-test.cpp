#include <gtest/gtest.h>

#include "config-backend.hpp"
#include "ini-file.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using wf_settings::ConfigBackend;

namespace
{

std::string tmp_ini(const std::string& body)
{
    char path[] = "/tmp/wf-cfg-backend-XXXXXX";
    int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    if (fd < 0)
    {
        return {};
    }
    {
        std::ofstream out(path);
        out << body;
    }
    close(fd);
    return path;
}

std::string read_all(const std::string& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

const char *kLiveLikeIni =
    "# keep me\n"
    "[core]\n"
    "plugins = \\\n"
    "  alpha \\\n"
    "  animate \\\n"
    "  autostart \\\n"
    "  command \\\n"
    "  decoration \\\n"
    "  expo \\\n"
    "  grid \\\n"
    "  zoom\n"
    "vwidth = 3\n"
    "vheight = 3\n"
    "\n"
    "[command]\n"
    "binding_terminal = <super> KEY_ENTER\n"
    "command_terminal = alacritty\n"
    "\n"
    "[output:HDMI-A-1]\n"
    "mode = 5120x1440@143984\n"
    "position = 0,0\n";

void isolate_env(const std::string& wayfire_path)
{
    setenv("WAYFIRE_CONFIG_FILE", wayfire_path.c_str(), 1);
    setenv("WF_SHELL_CONFIG_FILE", "/tmp/wf-shell-test-does-not-need-to-exist.ini", 1);
    setenv("XDG_CONFIG_HOME", "/tmp/wf-settings-test-xdg", 1);
}

void cleanup_bak(const std::string& path)
{
    unlink(path.c_str());
    unlink((path + ".bak-last").c_str());
}

} // namespace

/**
 * Save must only upsert dirty keys — never rewrite the whole wayfire.ini from
 * the in-memory XML catalog (that path crashed live Wayfire sessions).
 */
TEST(ConfigBackend, DirtySaveOnlyTouchesChangedKeys)
{
    auto path = tmp_ini(
        "# keep me\n"
        "[core]\n"
        "plugins = alpha animate command\n"
        "vwidth = 3\n"
        "vheight = 3\n"
        "\n"
        "[command]\n"
        "binding_terminal = <super> KEY_ENTER\n"
        "command_terminal = alacritty\n"
        "\n"
        "[output:HDMI-A-1]\n"
        "mode = 5120x1440@143984\n"
        "position = 0,0\n");
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();

    EXPECT_FALSE(b.has_dirty_wayfire());
    EXPECT_TRUE(b.set_wayfire_option("core", "vwidth", "5", nullptr));
    EXPECT_TRUE(b.has_dirty_wayfire());
    EXPECT_EQ(b.dirty_wayfire_count(), 1u);

    std::string err;
    ASSERT_TRUE(b.save_wayfire(&err)) << err;
    EXPECT_FALSE(b.has_dirty_wayfire());

    EXPECT_EQ(wf_shell::ini_get(path, "core", "vwidth"), "5");
    EXPECT_EQ(wf_shell::ini_get(path, "core", "plugins"), "alpha animate command");
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vheight"), "3");
    EXPECT_EQ(wf_shell::ini_get(path, "command", "command_terminal"), "alacritty");
    EXPECT_EQ(wf_shell::ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@143984");

    EXPECT_NE(read_all(path).find("# keep me"), std::string::npos);
    EXPECT_EQ(access((path + ".bak-last").c_str(), R_OK), 0);
    cleanup_bak(path);
}

TEST(ConfigBackend, SaveWithNoDirtyIsSoftSuccess)
{
    auto path = tmp_ini("[core]\nvwidth = 3\n");
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();
    EXPECT_FALSE(b.has_dirty_wayfire());
    std::string err;
    EXPECT_TRUE(b.save_wayfire(&err));
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vwidth"), "3");
    cleanup_bak(path);
}

TEST(ConfigBackend, MultipleDirtySectionsUpsertIndependently)
{
    auto path = tmp_ini(
        "[core]\n"
        "vwidth = 3\n"
        "vheight = 3\n"
        "[input]\n"
        "xkb_layout = us\n");
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();
    ASSERT_TRUE(b.set_wayfire_option("core", "vwidth", "6", nullptr));
    ASSERT_TRUE(b.set_wayfire_option("input", "xkb_layout", "gb", nullptr));
    EXPECT_EQ(b.dirty_wayfire_count(), 2u);

    std::string err;
    ASSERT_TRUE(b.save_wayfire(&err)) << err;
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vwidth"), "6");
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vheight"), "3");
    EXPECT_EQ(wf_shell::ini_get(path, "input", "xkb_layout"), "gb");
    cleanup_bak(path);
}

TEST(ConfigBackend, RejectedValueDoesNotDirty)
{
    auto path = tmp_ini("[core]\nvwidth = 3\n");
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();

    std::string err;
    bool ok = b.set_wayfire_option("core", "vwidth", "not-a-number", &err);
    if (!ok)
    {
        EXPECT_FALSE(b.has_dirty_wayfire());
    }
    (void)ok;
    cleanup_bak(path);
}

/**
 * Settings plugin enable/disable dirties core/plugins. Saving must not leave
 * orphan continuation lines from the previous multiline plugins= block.
 * That corruption reloads into a dying compositor.
 */
TEST(ConfigBackend, DirtyPluginsSaveDoesNotOrphanMultilineContinuations)
{
    auto path = tmp_ini(kLiveLikeIni);
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();

    /* Stage a plugins rewrite like PluginPage enable toggle + Save */
    const std::string plugins =
        "alpha animate autostart command decoration expo grid zoom";
    ASSERT_TRUE(b.set_wayfire_option("core", "plugins", plugins, nullptr));
    ASSERT_TRUE(b.has_dirty_wayfire());

    std::string err;
    ASSERT_TRUE(b.save_wayfire(&err)) << err;

    auto got = wf_shell::ini_get(path, "core", "plugins");
    EXPECT_NE(got.find("alpha"), std::string::npos);
    EXPECT_NE(got.find("grid"), std::string::npos);
    EXPECT_EQ(got.find('\\'), std::string::npos);

    /* vwidth still valid; command section untouched */
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vwidth"), "3");
    EXPECT_EQ(wf_shell::ini_get(path, "command", "command_terminal"), "alacritty");
    EXPECT_EQ(wf_shell::ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@143984");

    /* No orphan "  cube \\" style lines without a parent key */
    std::string all = read_all(path);
    EXPECT_NE(all.find("[core]"), std::string::npos);
    EXPECT_NE(all.find("# keep me"), std::string::npos);

    cleanup_bak(path);
}

TEST(ConfigBackend, DirtyVwidthOnMultilinePluginsIniPreservesPlugins)
{
    auto path = tmp_ini(kLiveLikeIni);
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();

    ASSERT_TRUE(b.set_wayfire_option("core", "vwidth", "4", nullptr));
    std::string err;
    ASSERT_TRUE(b.save_wayfire(&err)) << err;

    EXPECT_EQ(wf_shell::ini_get(path, "core", "vwidth"), "4");
    auto plugins = wf_shell::ini_get(path, "core", "plugins");
    EXPECT_NE(plugins.find("autostart"), std::string::npos);
    EXPECT_NE(plugins.find("zoom"), std::string::npos);
    EXPECT_EQ(wf_shell::ini_get(path, "output:HDMI-A-1", "position"), "0,0");
    cleanup_bak(path);
}

TEST(ConfigBackend, BackupCreatedBeforeSave)
{
    auto path = tmp_ini(kLiveLikeIni);
    ASSERT_FALSE(path.empty());
    isolate_env(path);

    auto& b = ConfigBackend::instance();
    b.reload();
    ASSERT_TRUE(b.set_wayfire_option("core", "vheight", "2", nullptr));
    std::string err;
    ASSERT_TRUE(b.save_wayfire(&err)) << err;
    EXPECT_EQ(access((path + ".bak-last").c_str(), R_OK), 0);

    /* Backup should still have original vheight=3 */
    EXPECT_EQ(wf_shell::ini_get(path + ".bak-last", "core", "vheight"), "3");
    EXPECT_EQ(wf_shell::ini_get(path, "core", "vheight"), "2");
    cleanup_bak(path);
}
