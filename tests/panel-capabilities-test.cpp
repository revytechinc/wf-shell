#include <gtest/gtest.h>

#include "panel-capabilities.hpp"
#include "theme-defaults.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using wf_shell::available_panel_widget_ids;
using wf_shell::panel_widget_available;
using wf_shell::resolve_theme_menu_icon_path;
using wf_shell::sanitize_panel_widgets_list;
using wf_shell::theme_artifacts_complete;
using wf_shell::theme_default_menu_icon_id;
using wf_shell::themed_menu_icon_theme_ids;

/* ── Never present widgets that this build cannot construct ───────────── */

TEST(PanelCapabilities, WeatherOnlyIfBuilt)
{
#ifdef HAVE_WEATHER
    EXPECT_TRUE(panel_widget_available("weather"));
#else
    EXPECT_FALSE(panel_widget_available("weather"))
        << "Settings must not present weather when panel is built without it";
#endif
}

TEST(PanelCapabilities, VolumeOnlyIfPulse)
{
#ifdef HAVE_PULSE
    EXPECT_TRUE(panel_widget_available("volume"));
#else
    EXPECT_FALSE(panel_widget_available("volume"));
#endif
}

TEST(PanelCapabilities, MixerOnlyIfWireplumber)
{
#ifdef HAVE_WIREPLUMBER
    EXPECT_TRUE(panel_widget_available("mixer"));
#else
    EXPECT_FALSE(panel_widget_available("mixer"));
#endif
}

TEST(PanelCapabilities, CoreWidgetsAlways)
{
    for (const char *id : {"menu", "launchers", "clock", "network", "battery",
             "window-list", "tray", "notifications", "spacing4", "spacing8"})
    {
        EXPECT_TRUE(panel_widget_available(id)) << id;
    }
}

TEST(PanelCapabilities, CatalogNeverListsUnavailable)
{
    auto avail = available_panel_widget_ids();
    for (const auto& id : avail)
    {
        EXPECT_TRUE(panel_widget_available(id)) << id;
    }
#ifndef HAVE_WEATHER
    EXPECT_TRUE(std::find(avail.begin(), avail.end(), "weather") == avail.end());
#endif
}

TEST(PanelCapabilities, SanitizeDropsWeatherWhenUnavailable)
{
    auto s = sanitize_panel_widgets_list(
        "menu spacing4 weather volume clock");
#ifndef HAVE_WEATHER
    EXPECT_EQ(s.find("weather"), std::string::npos)
        << "sanitized list still has weather: " << s;
#endif
    EXPECT_NE(s.find("menu"), std::string::npos);
    EXPECT_NE(s.find("clock"), std::string::npos);
}

TEST(PanelCapabilities, SanitizeUnknownBecomesNone)
{
    EXPECT_EQ(sanitize_panel_widgets_list("not-a-real-widget-xyz"), "none");
    EXPECT_EQ(sanitize_panel_widgets_list("none"), "none");
    EXPECT_EQ(sanitize_panel_widgets_list(""), "none");
}

/* ── Theme artifacts: CSS + menu icon required to present ─────────────── */

TEST(ThemeArtifacts, MissingCssIncomplete)
{
    EXPECT_FALSE(theme_artifacts_complete("nord",
        "/nonexistent/nord.css",
        "/usr/local/share/wf-shell/icons", {}));
}

TEST(ThemeArtifacts, MissingIconIncomplete)
{
    /* Synthetic theme: real CSS content path but icon id will not resolve. */
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "wf-shell-cap-test";
    fs::create_directories(tmp / "themes");
    fs::create_directories(tmp / "icons" / "menu");
    auto css = tmp / "themes" / "no-icon-theme.css";
    {
        std::ofstream o(css);
        o << "/* empty theme for artifact test */\n.wf-panel { color: red; }\n";
    }
    /* theme_default_menu_icon_id("no-icon-theme") → "wayfire"; strip wayfire too */
    EXPECT_FALSE(theme_artifacts_complete("no-icon-theme", css.string(),
        (tmp / "icons").string(), {}))
        << "must refuse theme when its menu icon is not under the icon root";
    fs::remove_all(tmp);
}

TEST(ThemeArtifacts, InstalledSystemThemesCompleteWhenIconsPresent)
{
    const std::string themes = "/usr/local/share/wf-shell/themes";
    const std::string icons  = "/usr/local/share/wf-shell/icons";
    if (!std::filesystem::is_directory(themes) ||
        !std::filesystem::is_directory(icons))
    {
        GTEST_SKIP() << "wf-shell not installed systemwide";
    }

    for (const auto& id : themed_menu_icon_theme_ids())
    {
        std::string css = themes + "/" + id + ".css";
        if (!std::filesystem::is_regular_file(css))
        {
            /* Not all themed ids may be shipped; incomplete must not pass */
            EXPECT_FALSE(theme_artifacts_complete(id, css, icons, {}))
                << "missing css should fail: " << id;
            continue;
        }
        /* If CSS is installed, icon MUST be present or we fail the product rule */
        EXPECT_TRUE(theme_artifacts_complete(id, css, icons, {}))
            << "theme " << id << " installed without menu icon "
            << theme_default_menu_icon_id(id);
    }
}

TEST(ThemeArtifacts, ResolveIconFindsHaikuLeaf)
{
    auto p = resolve_theme_menu_icon_path("haiku-leaf",
        "/usr/local/share/wf-shell/icons", {});
    if (p.empty())
    {
        GTEST_SKIP() << "haiku-leaf not installed";
    }
    EXPECT_TRUE(std::filesystem::is_regular_file(p));
}
