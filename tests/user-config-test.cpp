#include "user-config.hpp"
#include "ini-file.hpp"
#include "shell-json-config.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using wf_shell::ensure_parent_directories;
using wf_shell::ensure_settings_user_configs;
using wf_shell::ensure_user_config_file;
using wf_shell::ini_get;
using wf_shell::ini_set;
using wf_shell::settings_save_section;

namespace
{

std::string mkdtemp_dir()
{
    char tmpl[] = "/tmp/wf-user-cfg-XXXXXX";
    char *p = mkdtemp(tmpl);
    return p ? std::string(p) : std::string{};
}

void write_file(const std::string& path, const std::string& body)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream o(path);
    o << body;
}

} // namespace

TEST(UserConfig, EnsureParentCreatesNestedDir)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/a/b/c/file.ini";
    std::string err;
    EXPECT_TRUE(ensure_parent_directories(path, &err)) << err;
    EXPECT_TRUE(fs::is_directory(root + "/a/b/c"));
    fs::remove_all(root);
}

TEST(UserConfig, EnsureFileCreatesEmptyWhenNoSeed)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/.config/wf-shell.ini";
    auto r = ensure_user_config_file(path, {}, "# created\n");
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.created_dir);
    EXPECT_TRUE(r.created_file);
    EXPECT_FALSE(r.seeded_from_system);
    EXPECT_TRUE(fs::is_regular_file(path));
    std::ifstream in(path);
    std::string body((std::istreambuf_iterator<char>(in)), {});
    EXPECT_NE(body.find("created"), std::string::npos);
    fs::remove_all(root);
}

TEST(UserConfig, EnsureFileSeedsFromSystem)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string seed = root + "/system.ini";
    write_file(seed, "[panel]\nposition = top\n");
    std::string path = root + "/user/.config/wf-shell.ini";
    auto r = ensure_user_config_file(path, seed, "");
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.created_file);
    EXPECT_TRUE(r.seeded_from_system);
    EXPECT_EQ(ini_get(path, "panel", "position"), "top");
    fs::remove_all(root);
}

TEST(UserConfig, EnsureExistingWritableIsOkNoRecreate)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/wf-shell.ini";
    write_file(path, "[panel]\nposition = bottom\n");
    auto r = ensure_user_config_file(path, {}, "");
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.created_file);
    EXPECT_EQ(ini_get(path, "panel", "position"), "bottom");
    fs::remove_all(root);
}

TEST(UserConfig, EnsureFailsWhenParentNotWritable)
{
    /* Skip if root — cannot create unwritable dir as root easily */
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "root can write almost anywhere";
    }
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string locked = root + "/locked";
    fs::create_directories(locked);
    ASSERT_EQ(chmod(locked.c_str(), 0555), 0);
    std::string path = locked + "/sub/file.ini";
    auto r = ensure_user_config_file(path, {}, "");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
    chmod(locked.c_str(), 0755);
    fs::remove_all(root);
}

TEST(UserConfig, IniSetCreatesMissingFileAndDir)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/new/.config/wf-shell.ini";
    ASSERT_FALSE(fs::exists(path));
    ASSERT_TRUE(ini_set(path, "panel", "position", "top"));
    EXPECT_TRUE(fs::is_regular_file(path));
    EXPECT_EQ(ini_get(path, "panel", "position"), "top");
    fs::remove_all(root);
}

TEST(UserConfig, SettingsSaveSectionCreatesAllUserArtifacts)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    setenv("HOME", root.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root + "/.config").c_str(), 1);
    unsetenv("WF_SHELL_CONFIG_FILE");
    unsetenv("WF_SHELL_JSON_CONFIG");
    unsetenv("WAYFIRE_CONFIG_FILE");

    /* Wipe so first-run path runs */
    fs::remove_all(root + "/.config");

    std::string err;
    ASSERT_TRUE(settings_save_section("panel", {{"position", "top"}}, &err)) << err;

    EXPECT_TRUE(fs::is_regular_file(root + "/.config/wf-shell.ini"));
    EXPECT_TRUE(fs::is_regular_file(root + "/.config/wf-shell/config.json"));
    EXPECT_TRUE(fs::is_regular_file(root + "/.config/wayfire.ini"));
    EXPECT_EQ(ini_get(root + "/.config/wf-shell.ini", "panel", "position"), "top");

    /* If package system seed exists, panel position top should be present from seed
     * before our write — either way our key is top. */
    if (fs::exists("/usr/local/etc/wf-shell/wf-shell.ini"))
    {
        /* Seeded copy may have full system defaults + our override */
        EXPECT_EQ(ini_get(root + "/.config/wf-shell.ini", "panel", "position"), "top");
    }

    fs::remove_all(root);
}

TEST(UserConfig, SettingsSaveReportsFailureOnUnwritableTree)
{
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "root can write almost anywhere";
    }
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string locked = root + "/lockedhome";
    fs::create_directories(locked);
    ASSERT_EQ(chmod(locked.c_str(), 0555), 0);

    setenv("HOME", locked.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (locked + "/.config").c_str(), 1);
    unsetenv("WF_SHELL_CONFIG_FILE");
    unsetenv("WF_SHELL_JSON_CONFIG");
    unsetenv("WAYFIRE_CONFIG_FILE");

    std::string err;
    EXPECT_FALSE(settings_save_section("panel", {{"position", "top"}}, &err));
    EXPECT_FALSE(err.empty());

    chmod(locked.c_str(), 0755);
    fs::remove_all(root);
}
