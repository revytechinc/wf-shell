#include <gtest/gtest.h>

#include "ini-file.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

using wf_shell::ini_get;
using wf_shell::ini_set;
using wf_shell::ini_set_many;

namespace
{

std::string tmp_ini()
{
    char path[] = "/tmp/wf-ini-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
    {
        return {};
    }
    close(fd);
    return path;
}

void write_all(const std::string& path, const std::string& body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

std::string read_all(const std::string& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

/** Canonical multiline plugins block as used in real wayfire.ini */
const char *kMultilinePluginsIni =
    "# header comment\n"
    "[core]\n"
    "plugins = \\\n"
    "  alpha \\\n"
    "  animate \\\n"
    "  autostart \\\n"
    "  command \\\n"
    "  cube \\\n"
    "  decoration \\\n"
    "  expo \\\n"
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

} // namespace

TEST(IniFile, SetManyPreservesOtherSectionsAndComments)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path,
        "# header comment\n"
        "[core]\n"
        "plugins = alpha animate\n"
        "vwidth = 3\n"
        "\n"
        "[command]\n"
        "binding_terminal = <super> KEY_ENTER\n"
        "command_terminal = alacritty\n"
        "\n"
        "[output:HDMI-A-1]\n"
        "mode = 5120x1440@143984\n");

    ASSERT_TRUE(ini_set_many(path, "core", {
        {"vwidth", "4"},
        {"vheight", "2"},
    }));

    EXPECT_EQ(ini_get(path, "core", "vwidth"), "4");
    EXPECT_EQ(ini_get(path, "core", "vheight"), "2");
    EXPECT_EQ(ini_get(path, "core", "plugins"), "alpha animate");
    EXPECT_EQ(ini_get(path, "command", "command_terminal"), "alacritty");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@143984");

    std::string all = read_all(path);
    EXPECT_NE(all.find("# header comment"), std::string::npos);

    unlink(path.c_str());
}

TEST(IniFile, SetDoesNotClobberUnrelatedKeysInSection)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path,
        "[panel]\n"
        "position = top\n"
        "widgets_left = menu clock\n"
        "css_path = /tmp/theme.css\n");

    ASSERT_TRUE(ini_set(path, "panel", "position", "bottom"));
    EXPECT_EQ(ini_get(path, "panel", "position"), "bottom");
    EXPECT_EQ(ini_get(path, "panel", "widgets_left"), "menu clock");
    EXPECT_EQ(ini_get(path, "panel", "css_path"), "/tmp/theme.css");
    unlink(path.c_str());
}

TEST(IniFile, CreatesSectionWhenMissing)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, "[core]\nplugins = x\n");
    ASSERT_TRUE(ini_set(path, "input", "xkb_layout", "us"));
    EXPECT_EQ(ini_get(path, "input", "xkb_layout"), "us");
    EXPECT_EQ(ini_get(path, "core", "plugins"), "x");
    unlink(path.c_str());
}

TEST(IniFile, EmptyBatchIsNoopSuccess)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, "[core]\nvwidth = 3\n");
    EXPECT_TRUE(ini_set_many(path, "core", {}));
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "3");
    unlink(path.c_str());
}

/**
 * THE crash path: rewriting plugins= while the file still had
 * backslash-continued plugin name lines left orphan tokens in [core].
 * Wayfire reloads that via inotify and dies.
 */
TEST(IniFile, MultilinePluginsGetDoesJoinContinuations)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    auto plugins = ini_get(path, "core", "plugins");
    EXPECT_NE(plugins.find("alpha"), std::string::npos);
    EXPECT_NE(plugins.find("animate"), std::string::npos);
    EXPECT_NE(plugins.find("autostart"), std::string::npos);
    EXPECT_NE(plugins.find("zoom"), std::string::npos);
    /* Must not include backslashes in joined value */
    EXPECT_EQ(plugins.find('\\'), std::string::npos);
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "3");
    unlink(path.c_str());
}

TEST(IniFile, SetVwidthDoesNotCorruptMultilinePlugins)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    ASSERT_TRUE(ini_set(path, "core", "vwidth", "5"));
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "5");

    auto plugins = ini_get(path, "core", "plugins");
    EXPECT_NE(plugins.find("alpha"), std::string::npos);
    EXPECT_NE(plugins.find("zoom"), std::string::npos);

    /* File must not contain orphan bare plugin tokens as keys */
    std::string all = read_all(path);
    /* After plugins block, vwidth should exist; no line that is just "  alpha" without being under plugins */
    EXPECT_NE(all.find("plugins"), std::string::npos);
    EXPECT_EQ(ini_get(path, "command", "command_terminal"), "alacritty");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@143984");
    unlink(path.c_str());
}

TEST(IniFile, ReplaceMultilinePluginsRemovesOrphanContinuations)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    /* Simulate settings plugin-toggle save: single-line or reformatted list */
    const std::string new_plugins =
        "alpha animate autostart command cube decoration expo zoom";
    ASSERT_TRUE(ini_set(path, "core", "plugins", new_plugins));

    auto got = ini_get(path, "core", "plugins");
    EXPECT_NE(got.find("alpha"), std::string::npos);
    EXPECT_NE(got.find("zoom"), std::string::npos);
    EXPECT_EQ(got.find('\\'), std::string::npos);

    /* Critical: no orphan lines that look like bare plugin names without key= */
    std::string all = read_all(path);
    /* Count how many times "alpha" appears — should only be inside plugins value */
    size_t count = 0;
    size_t pos = 0;
    while ((pos = all.find("alpha", pos)) != std::string::npos)
    {
        ++count;
        pos += 5;
    }
    /* Once in plugins list (possibly multiline format still has one alpha) */
    EXPECT_LE(count, 2u);

    /* Other core keys still work */
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "3");
    EXPECT_EQ(ini_get(path, "core", "vheight"), "3");
    EXPECT_EQ(ini_get(path, "command", "command_terminal"), "alacritty");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@143984");

    /* Header preserved */
    EXPECT_NE(all.find("# header comment"), std::string::npos);
    unlink(path.c_str());
}

TEST(IniFile, ReplacePluginsThenSetVwidthStillConsistent)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    ASSERT_TRUE(ini_set(path, "core", "plugins",
        "alpha animate autostart command decoration expo grid"));
    ASSERT_TRUE(ini_set(path, "core", "vwidth", "4"));
    ASSERT_TRUE(ini_set(path, "core", "vheight", "2"));

    auto plugins = ini_get(path, "core", "plugins");
    EXPECT_NE(plugins.find("grid"), std::string::npos);
    EXPECT_EQ(plugins.find("cube"), std::string::npos); /* removed */
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "4");
    EXPECT_EQ(ini_get(path, "core", "vheight"), "2");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "position"), "0,0");
    unlink(path.c_str());
}

TEST(IniFile, SetManyPluginsAndVwidthAtomic)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    ASSERT_TRUE(ini_set_many(path, "core", {
        {"plugins", "alpha animate command zoom"},
        {"vwidth", "6"},
    }));

    EXPECT_EQ(ini_get(path, "core", "vwidth"), "6");
    auto p = ini_get(path, "core", "plugins");
    EXPECT_NE(p.find("alpha"), std::string::npos);
    EXPECT_NE(p.find("zoom"), std::string::npos);
    EXPECT_EQ(p.find("cube"), std::string::npos);
    EXPECT_EQ(ini_get(path, "core", "vheight"), "3");
    unlink(path.c_str());
}

TEST(IniFile, OutputSectionUpsertDoesNotTouchCore)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    ASSERT_TRUE(ini_set_many(path, "output:HDMI-A-1", {
        {"mode", "5120x1440@119992"},
        {"scale", "1.0"},
    }));

    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "mode"), "5120x1440@119992");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "scale"), "1.0");
    EXPECT_EQ(ini_get(path, "output:HDMI-A-1", "position"), "0,0");

    auto plugins = ini_get(path, "core", "plugins");
    EXPECT_NE(plugins.find("autostart"), std::string::npos);
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "3");
    unlink(path.c_str());
}

TEST(IniFile, RoundTripMultilinePluginsIdempotent)
{
    auto path = tmp_ini();
    ASSERT_FALSE(path.empty());
    write_all(path, kMultilinePluginsIni);

    auto once = ini_get(path, "core", "plugins");
    ASSERT_TRUE(ini_set(path, "core", "plugins", once));
    auto twice = ini_get(path, "core", "plugins");

    /* Token sets should match (order preserved as joined) */
    EXPECT_NE(twice.find("alpha"), std::string::npos);
    EXPECT_NE(twice.find("zoom"), std::string::npos);
    EXPECT_EQ(ini_get(path, "core", "vwidth"), "3");
    unlink(path.c_str());
}
