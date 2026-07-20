/**
 * CSS / theme path load safety — missing, empty, huge, and valid files must
 * never throw; bad paths return null so panel keeps default styles.
 *
 * Does not depend on data/themes packs content (other agents may edit packs).
 */
#include <gtest/gtest.h>

#include "gtk-utils.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include <gtkmm.h>

namespace fs = std::filesystem;

class CssLoadTest : public ::testing::Test
{
  protected:
    fs::path tmpdir;
    void SetUp() override
    {
        tmpdir = fs::temp_directory_path() /
            ("wf-css-test-" + std::to_string(::getpid()));
        fs::create_directories(tmpdir);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tmpdir, ec);
    }
    fs::path write_file(const std::string& name, const std::string& body)
    {
        auto p = tmpdir / name;
        std::ofstream out(p);
        out << body;
        return p;
    }
};

TEST_F(CssLoadTest, EmptyPathRejectedUnlessAllowEmpty)
{
    auto a = check_css_file_path("", false);
    EXPECT_FALSE(a.ok);
    EXPECT_FALSE(a.reason.empty());

    auto b = check_css_file_path("", true);
    EXPECT_TRUE(b.ok);
}

TEST_F(CssLoadTest, MissingPathRejected)
{
    auto r = check_css_file_path((tmpdir / "nope.css").string());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.reason.find("does not exist"), std::string::npos);
}

TEST_F(CssLoadTest, EmptyFileRejected)
{
    auto p = write_file("empty.css", "");
    auto r = check_css_file_path(p.string());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.reason.find("empty"), std::string::npos);
}

TEST_F(CssLoadTest, ValidCssPathAccepted)
{
    auto p = write_file("ok.css", ".panel { color: red; }\n");
    auto r = check_css_file_path(p.string());
    EXPECT_TRUE(r.ok) << r.reason;
}

TEST_F(CssLoadTest, LoadMissingReturnsNull)
{
    auto css = load_css_from_path((tmpdir / "missing.css").string());
    EXPECT_FALSE(css);
}

TEST_F(CssLoadTest, LoadValidReturnsProvider)
{
    auto p = write_file("theme.css",
        "/* Name: Unit Test */\n.wf-panel { background: #111; }\n");
    auto css = load_css_from_path(p.string());
    EXPECT_TRUE(css);
}

TEST_F(CssLoadTest, LoadStringSafeNeverThrows)
{
    std::string err;
    auto ok = load_css_from_string_safe(".x { color: blue; }", &err);
    EXPECT_TRUE(ok);

    auto empty = load_css_from_string_safe("", &err);
    EXPECT_TRUE(empty); /* empty string is valid CSS */

    /* Garbage: GTK may warn but typically still returns a provider */
    auto junk = load_css_from_string_safe("{{{{ not css", &err);
    /* Must not throw; null or provider both acceptable as long as stable */
    (void)junk;
}

TEST_F(CssLoadTest, OversizedRejected)
{
    auto p = tmpdir / "huge.css";
    {
        std::ofstream out(p);
        out << std::string(3 * 1024 * 1024, 'x');
    }
    auto r = check_css_file_path(p.string());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.reason.find("too large"), std::string::npos);
    EXPECT_FALSE(load_css_from_path(p.string()));
}

int main(int argc, char **argv)
{
    /* CssProvider needs a display type backend in some builds */
    Gtk::Application::create("org.revytech.wf-shell.css-load-test");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
