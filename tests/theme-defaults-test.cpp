#include <gtest/gtest.h>

#include "theme-defaults.hpp"

#include <set>
#include <string>

using wf_shell::ThemeSelectionState;
using wf_shell::clear_theme_to_defaults;
using wf_shell::is_default_theme_id;
using wf_shell::menu_icon_logical_candidates;
using wf_shell::select_theme;
using wf_shell::theme_default_menu_icon_id;
using wf_shell::theme_defaults_are_restored;
using wf_shell::theme_id_from_css_path;
using wf_shell::themed_menu_icon_theme_ids;

/* ── theme_id_from_css_path ─────────────────────────────────────────────── */

TEST(ThemeIdFromCssPath, EmptyIsDefault)
{
    EXPECT_EQ(theme_id_from_css_path(""), "default");
    EXPECT_EQ(theme_id_from_css_path("   "), "default");
    EXPECT_EQ(theme_id_from_css_path("\t\n"), "default");
}

TEST(ThemeIdFromCssPath, StemFromPath)
{
    EXPECT_EQ(theme_id_from_css_path("/home/u/.local/share/wf-shell/themes/crt-phosphor.css"),
        "crt-phosphor");
    EXPECT_EQ(theme_id_from_css_path("win95.css"), "win95");
    EXPECT_EQ(theme_id_from_css_path("/tmp/nord.css"), "nord");
}

TEST(ThemeIdFromCssPath, DefaultStemIsDefault)
{
    EXPECT_EQ(theme_id_from_css_path("/x/default.css"), "default");
}

/* ── menu icon map ──────────────────────────────────────────────────────── */

TEST(ThemeMenuIconMap, DefaultIsWayfire)
{
    EXPECT_EQ(theme_default_menu_icon_id("default"), "wayfire");
    EXPECT_EQ(theme_default_menu_icon_id(""), "wayfire");
    EXPECT_EQ(theme_default_menu_icon_id("unknown-theme-xyz"), "wayfire");
}

TEST(ThemeMenuIconMap, KnownThemesHaveDistinctPackIds)
{
    EXPECT_EQ(theme_default_menu_icon_id("crt-phosphor"), "crt-node");
    EXPECT_EQ(theme_default_menu_icon_id("win95"), "win95-start");
    EXPECT_EQ(theme_default_menu_icon_id("amiga-workbench"), "amiga-wb");
    EXPECT_EQ(theme_default_menu_icon_id("miami-cyberpunk"), "neon-orb");
    EXPECT_EQ(theme_default_menu_icon_id("system7"), "system7-apple");
}

TEST(ThemeMenuIconMap, EveryThemedIdMapsAwayFromBareWayfire)
{
    for (const auto& id : themed_menu_icon_theme_ids())
    {
        auto icon = theme_default_menu_icon_id(id);
        EXPECT_FALSE(icon.empty()) << id;
        EXPECT_NE(icon, "wayfire") << "themed pack should not use bare wayfire id: " << id;
    }
}

/* ── select → clear → defaults ──────────────────────────────────────────── */

TEST(ThemeSelectAndClear, CrtThenClearRestoresDefaults)
{
    auto applied = select_theme("crt-phosphor", "/opt/wf-shell/themes");
    EXPECT_EQ(applied.theme_id, "crt-phosphor");
    EXPECT_EQ(applied.menu_icon_id, "crt-node");
    EXPECT_EQ(applied.css_path, "/opt/wf-shell/themes/crt-phosphor.css");
    EXPECT_FALSE(theme_defaults_are_restored(applied));

    auto cleared = clear_theme_to_defaults();
    EXPECT_TRUE(theme_defaults_are_restored(cleared));
    EXPECT_EQ(cleared.theme_id, "default");
    EXPECT_TRUE(cleared.css_path.empty());
    EXPECT_EQ(cleared.menu_icon_id, "wayfire");
}

TEST(ThemeSelectAndClear, Win95ThenClearRestoresDefaults)
{
    auto applied = select_theme("win95");
    EXPECT_EQ(applied.theme_id, "win95");
    EXPECT_EQ(applied.menu_icon_id, "win95-start");
    EXPECT_FALSE(applied.css_path.empty());

    auto cleared = clear_theme_to_defaults();
    EXPECT_TRUE(theme_defaults_are_restored(cleared));
    EXPECT_EQ(cleared.menu_icon_id, "wayfire");
}

TEST(ThemeSelectAndClear, AllPackThemesClearToWayfire)
{
    for (const auto& id : themed_menu_icon_theme_ids())
    {
        auto applied = select_theme(id, "/themes");
        EXPECT_EQ(applied.theme_id, id);
        EXPECT_NE(applied.menu_icon_id, "wayfire") << id;
        EXPECT_EQ(theme_id_from_css_path(applied.css_path), id);

        auto cleared = clear_theme_to_defaults();
        EXPECT_TRUE(theme_defaults_are_restored(cleared)) << "after clearing " << id;
        EXPECT_EQ(theme_default_menu_icon_id(
                      theme_id_from_css_path(cleared.css_path)),
            "wayfire")
            << "default theme id must map to wayfire menu icon";
    }
}

TEST(ThemeSelectAndClear, SelectingDefaultIsSameAsClear)
{
    auto a = select_theme("default");
    auto b = clear_theme_to_defaults();
    EXPECT_EQ(a.theme_id, b.theme_id);
    EXPECT_EQ(a.css_path, b.css_path);
    EXPECT_EQ(a.menu_icon_id, b.menu_icon_id);
    EXPECT_TRUE(theme_defaults_are_restored(a));
}

TEST(ThemeSelectAndClear, RoundTripCssPathToIcon)
{
    /* Simulate: user picks CRT, css_path written, then set empty again */
    auto on = select_theme("crt-phosphor", "/share/wf-shell/themes");
    std::string stored_css = on.css_path;
    EXPECT_EQ(theme_id_from_css_path(stored_css), "crt-phosphor");
    EXPECT_EQ(theme_default_menu_icon_id(theme_id_from_css_path(stored_css)), "crt-node");

    std::string cleared_css; /* empty like update_ini_css_path("") */
    EXPECT_EQ(theme_id_from_css_path(cleared_css), "default");
    EXPECT_EQ(theme_default_menu_icon_id(theme_id_from_css_path(cleared_css)), "wayfire");
}

/* ── candidates / invariants ────────────────────────────────────────────── */

TEST(ThemeMenuIconCandidates, DefaultLeadsWithWayfire)
{
    auto c = menu_icon_logical_candidates("default");
    ASSERT_FALSE(c.empty());
    EXPECT_EQ(c.front(), "wayfire");
}

TEST(ThemeMenuIconCandidates, ThemedLeadsWithPackThenWayfireFallback)
{
    auto c = menu_icon_logical_candidates("crt-phosphor");
    ASSERT_GE(c.size(), 2u);
    EXPECT_EQ(c[0], "crt-node");
    EXPECT_EQ(c[1], "wayfire");
}

TEST(ThemeMenuIconCandidates, AlwaysIncludeExecutableFallback)
{
    for (const auto& id : themed_menu_icon_theme_ids())
    {
        auto c = menu_icon_logical_candidates(id);
        EXPECT_NE(std::find(c.begin(), c.end(), "application-x-executable"), c.end())
            << id;
    }
    auto d = menu_icon_logical_candidates("default");
    EXPECT_NE(std::find(d.begin(), d.end(), "application-x-executable"), d.end());
}

TEST(ThemeDefaults, IsDefaultThemeId)
{
    EXPECT_TRUE(is_default_theme_id("default"));
    EXPECT_TRUE(is_default_theme_id(""));
    EXPECT_FALSE(is_default_theme_id("crt-phosphor"));
    EXPECT_FALSE(is_default_theme_id("win95"));
}
