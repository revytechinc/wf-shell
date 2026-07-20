#include "shell-json-config.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using wf_shell::load_shell_json_config_resilient;
using wf_shell::make_baseline_shell_json;
using wf_shell::parse_shell_json_config;
using wf_shell::save_shell_json_config;
using wf_shell::serialize_shell_json_config;
using wf_shell::shell_json_get;
using wf_shell::ShellJsonLoadSource;
using wf_shell::validate_shell_json_text;

namespace
{

std::string mkdtemp_dir()
{
    char tmpl[] = "/tmp/wf-json-XXXXXX";
    char *p = mkdtemp(tmpl);
    return p ? std::string(p) : std::string{};
}

void write_text(const std::string& path, const std::string& body)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream o(path);
    o << body;
}

std::string read_text(const std::string& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

} // namespace

TEST(ShellJsonValidate, EmptyIsHardFail)
{
    auto v = validate_shell_json_text("");
    EXPECT_FALSE(v.ok);
    EXPECT_FALSE(v.hard_errors.empty());
}

TEST(ShellJsonValidate, NotObjectIsHardFail)
{
    auto v = validate_shell_json_text("[1,2,3]");
    EXPECT_FALSE(v.ok);
}

TEST(ShellJsonValidate, ParseErrorIsHardFail)
{
    auto v = validate_shell_json_text("{not json");
    EXPECT_FALSE(v.ok);
}

TEST(ShellJsonValidate, ValidMinimalOk)
{
    auto v = validate_shell_json_text("{\"version\":1,\"mcp\":{\"enabled\":false,\"servers\":[]}}");
    EXPECT_TRUE(v.ok) << v.summary();
}

TEST(ShellJsonValidate, UnknownRootKeySoftIgnore)
{
    auto v = validate_shell_json_text(
        "{\"version\":1,\"bogus\":true,\"mcp\":{\"enabled\":false,\"servers\":[]}}");
    EXPECT_TRUE(v.ok) << v.summary();
    EXPECT_FALSE(v.ignored_keys.empty());
}

TEST(ShellJsonParse, IgnoresUnknownRootAndKeepsPanel)
{
    const char *text = R"json({
  "version": 1,
  "future_feature": 42,
  "panel": { "position": "top", "layer": "top" },
  "mcp": { "enabled": false, "servers": [] }
})json";
    wf_shell::ShellJsonConfig cfg;
    wf_shell::ShellJsonValidateResult vr;
    ASSERT_TRUE(parse_shell_json_config(text, cfg, nullptr, &vr));
    EXPECT_TRUE(vr.ok);
    EXPECT_EQ(shell_json_get(cfg, "panel", "position"), "top");
    EXPECT_FALSE(vr.ignored_keys.empty());
}

TEST(ShellJsonSerialize, RoundTripValidates)
{
    auto cfg = make_baseline_shell_json();
    cfg.sections["panel"]["position"] = "top";
    auto text = serialize_shell_json_config(cfg);
    auto v = validate_shell_json_text(text);
    EXPECT_TRUE(v.ok) << v.summary();
    wf_shell::ShellJsonConfig back;
    ASSERT_TRUE(parse_shell_json_config(text, back, nullptr));
    EXPECT_EQ(shell_json_get(back, "panel", "position"), "top");
}

TEST(ShellJsonSave, WritesLastGoodOnSuccess)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/config.json";
    auto cfg = make_baseline_shell_json();
    cfg.sections["panel"]["position"] = "top";
    std::string err;
    ASSERT_TRUE(save_shell_json_config(path, cfg, &err)) << err;
    EXPECT_TRUE(fs::is_regular_file(path));
    EXPECT_TRUE(fs::is_regular_file(path + ".last-good"));
    auto v = validate_shell_json_text(read_text(path));
    EXPECT_TRUE(v.ok);
    auto v2 = validate_shell_json_text(read_text(path + ".last-good"));
    EXPECT_TRUE(v2.ok);
    fs::remove_all(root);
}

TEST(ShellJsonLoadResilient, MissingIsNotOkMissingSource)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    auto r = load_shell_json_config_resilient(root + "/nope/config.json");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.source, ShellJsonLoadSource::missing);
    fs::remove_all(root);
}

TEST(ShellJsonLoadResilient, InvalidPrimaryUsesLastGood)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/config.json";
    auto good = make_baseline_shell_json();
    good.sections["panel"]["position"] = "top";
    ASSERT_TRUE(save_shell_json_config(path, good, nullptr));
    /* Corrupt primary; leave last-good intact */
    write_text(path, "{ this is not valid json !!!");
    auto r = load_shell_json_config_resilient(path);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.source, ShellJsonLoadSource::last_good);
    EXPECT_EQ(shell_json_get(r.cfg, "panel", "position"), "top");
    EXPECT_FALSE(r.quarantined_path.empty());
    EXPECT_TRUE(fs::is_regular_file(r.quarantined_path));
    /* Primary rewritten from last-good */
    EXPECT_TRUE(validate_shell_json_text(read_text(path)).ok);
    fs::remove_all(root);
}

TEST(ShellJsonLoadResilient, AllInvalidWritesBaseline)
{
    auto root = mkdtemp_dir();
    ASSERT_FALSE(root.empty());
    std::string path = root + "/config.json";
    write_text(path, "null");
    write_text(path + ".last-good", "[1,2,3]");
    auto r = load_shell_json_config_resilient(path);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.source, ShellJsonLoadSource::baseline);
    EXPECT_EQ(shell_json_get(r.cfg, "panel", "position"), "top");
    EXPECT_TRUE(validate_shell_json_text(read_text(path)).ok);
    EXPECT_TRUE(fs::is_regular_file(path + ".last-good"));
    /* Quarantine dir should have junk */
    auto qdir = root + "/quarantine";
    EXPECT_TRUE(fs::is_directory(qdir));
    fs::remove_all(root);
}

TEST(ShellJsonSave, RefuseIfSomehowUnserializableIsNotAnIssue)
{
    /* Baseline always serializes validly */
    auto cfg = make_baseline_shell_json();
    auto text = serialize_shell_json_config(cfg);
    EXPECT_TRUE(validate_shell_json_text(text).ok);
}
