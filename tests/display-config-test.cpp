#include <gtest/gtest.h>

#include "display-config.hpp"

using wf_shell::DisplayMode;
using wf_shell::DisplayOutput;
using wf_shell::apply_and_persist_display;
using wf_shell::apply_display_mode;
using wf_shell::parse_wlr_randr_json;
using wf_shell::persist_output_to_kanshi;
using wf_shell::persist_output_to_wayfire_ini;
using wf_shell::refresh_hz_to_millihertz;
using wf_shell::DisplayConfigHooks;

TEST(DisplayConfig, MillihertzRound)
{
    EXPECT_EQ(refresh_hz_to_millihertz(143.983994), 143984);
    EXPECT_EQ(refresh_hz_to_millihertz(119.991997), 119992);
    EXPECT_EQ(refresh_hz_to_millihertz(60.0), 60000);
}

TEST(DisplayConfig, ModeStrings)
{
    DisplayMode m;
    m.width = 5120;
    m.height = 1440;
    m.refresh_hz = 143.983994;
    EXPECT_EQ(m.wayfire_mode_string(), "5120x1440@143984");
    EXPECT_NE(m.wlr_mode_arg().find("5120x1440@"), std::string::npos);
    EXPECT_NE(m.label().find("5120"), std::string::npos);
}

TEST(DisplayConfig, ParseJsonDiscoversModesOnly)
{
    const char *sample = R"json(
[
  {
    "name": "HDMI-A-1",
    "description": "Samsung Odyssey G91F (HDMI-A-1)",
    "make": "Samsung",
    "model": "Odyssey G91F",
    "serial": "X",
    "enabled": true,
    "modes": [
      {"width": 3840, "height": 1080, "refresh": 119.969, "preferred": true, "current": false},
      {"width": 5120, "height": 1440, "refresh": 143.983994, "preferred": false, "current": true},
      {"width": 5120, "height": 1440, "refresh": 119.991997, "preferred": false, "current": false}
    ],
    "position": {"x": 0, "y": 0},
    "scale": 1.0,
    "transform": "normal"
  }
]
)json";
    auto r = parse_wlr_randr_json(sample);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.outputs.size(), 1u);
    const auto& o = r.outputs[0];
    EXPECT_EQ(o.name, "HDMI-A-1");
    ASSERT_EQ(o.modes.size(), 3u);
    auto cur = o.current_mode();
    EXPECT_TRUE(cur.valid());
    EXPECT_EQ(cur.width, 5120);
    EXPECT_EQ(cur.height, 1440);
    EXPECT_TRUE(o.supports(cur));

    DisplayMode fake;
    fake.width = 9999;
    fake.height = 9999;
    fake.refresh_hz = 60.0;
    EXPECT_FALSE(o.supports(fake));
}

/**
 * apply_and_persist must refuse unknown modes before any write/cmd.
 * Display page no longer probes in ctor — unit-level gate stays here.
 */
TEST(DisplayConfig, ApplyAndPersistRefusesUnsafeMode)
{
    using wf_shell::apply_and_persist_display;

    DisplayOutput o;
    o.name = "HDMI-A-1";
    DisplayMode real;
    real.width = 5120;
    real.height = 1440;
    real.refresh_hz = 144.0;
    o.modes.push_back(real);

    DisplayMode fake = real;
    fake.width = 9999;

    int cmds = 0;
    std::string written;
    DisplayConfigHooks h;
    h.run_cmd = [&] (const std::string&, std::string&, std::string&) {
        ++cmds;
        return 0;
    };
    h.read_file = [] (const std::string&) { return std::string("#\n"); };
    h.write_file = [&] (const std::string&, const std::string& c) {
        written = c;
        return true;
    };
    h.path_exists = [] (const std::string&) { return false; };

    std::string err;
    EXPECT_FALSE(apply_and_persist_display(o, fake, "/tmp/t.ini", "", &h, &err));
    EXPECT_EQ(cmds, 0); /* must not call wlr-randr */
    EXPECT_TRUE(written.empty());
    EXPECT_FALSE(err.empty());

    EXPECT_TRUE(apply_and_persist_display(o, real, "/tmp/t.ini", "", &h, &err));
    EXPECT_EQ(cmds, 1);
    EXPECT_NE(written.find("[output:HDMI-A-1]"), std::string::npos);
}

TEST(DisplayConfig, ApplyModeDoesNotWriteIni)
{
    using wf_shell::apply_display_mode;

    DisplayOutput o;
    o.name = "HDMI-A-1";
    DisplayMode m;
    m.width = 1920;
    m.height = 1080;
    m.refresh_hz = 60.0;
    o.modes.push_back(m);

    int writes = 0;
    int cmds = 0;
    DisplayConfigHooks h;
    h.run_cmd = [&] (const std::string& cmd, std::string&, std::string&) {
        ++cmds;
        EXPECT_NE(cmd.find("wlr-randr"), std::string::npos);
        EXPECT_NE(cmd.find("HDMI-A-1"), std::string::npos);
        return 0;
    };
    h.write_file = [&] (const std::string&, const std::string&) {
        ++writes;
        return true;
    };

    std::string err;
    EXPECT_TRUE(apply_display_mode(o, m, &h, &err));
    EXPECT_EQ(cmds, 1);
    EXPECT_EQ(writes, 0); /* live apply only — persist is separate */
}

TEST(DisplayConfig, PersistDoesNotRunWlrRandr)
{
    DisplayOutput o;
    o.name = "HDMI-A-1";
    DisplayMode m;
    m.width = 5120;
    m.height = 1440;
    m.refresh_hz = 143.983994;
    o.modes.push_back(m);

    int cmds = 0;
    std::string written;
    DisplayConfigHooks h;
    h.run_cmd = [&] (const std::string&, std::string&, std::string&) {
        ++cmds;
        return 0;
    };
    h.read_file = [] (const std::string&) {
        return std::string(
            "[core]\nplugins = alpha\n\n[output:HDMI-A-1]\nmode = old\n");
    };
    h.write_file = [&] (const std::string&, const std::string& c) {
        written = c;
        return true;
    };

    std::string err;
    EXPECT_TRUE(persist_output_to_wayfire_ini("/tmp/t.ini", o, m, &h, &err));
    EXPECT_EQ(cmds, 0);
    EXPECT_NE(written.find("5120x1440@143984"), std::string::npos);
    EXPECT_NE(written.find("plugins = alpha"), std::string::npos); /* core kept */
}

TEST(DisplayConfig, RefuseUnsafePersist)
{
    DisplayOutput o;
    o.name = "HDMI-A-1";
    DisplayMode real;
    real.width = 5120;
    real.height = 1440;
    real.refresh_hz = 144.0;
    o.modes.push_back(real);

    DisplayMode fake = real;
    fake.width = 8000;

    std::string written;
    DisplayConfigHooks h;
    h.read_file = [] (const std::string&) { return std::string("# empty\n"); };
    h.write_file = [&] (const std::string&, const std::string& c) {
        written = c;
        return true;
    };

    std::string err;
    EXPECT_FALSE(persist_output_to_wayfire_ini("/tmp/t.ini", o, fake, &h, &err));
    EXPECT_TRUE(written.empty());
    EXPECT_FALSE(err.empty());

    EXPECT_TRUE(persist_output_to_wayfire_ini("/tmp/t.ini", o, real, &h, &err));
    EXPECT_NE(written.find("[output:HDMI-A-1]"), std::string::npos);
    EXPECT_NE(written.find("5120x1440@"), std::string::npos);
}

TEST(DisplayConfig, KanshiDoesNotClobberExistingMainConfig)
{
    DisplayOutput o;
    o.name = "HDMI-A-1";
    o.description = "Samsung Odyssey";
    o.pos_x = 0;
    o.pos_y = 0;
    o.scale = 1.0;
    DisplayMode real;
    real.width = 5120;
    real.height = 1440;
    real.refresh_hz = 143.983994;
    o.modes.push_back(real);

    std::map<std::string, std::string> written;
    DisplayConfigHooks h;
    h.path_exists = [] (const std::string& p) {
        /* Main kanshi config already exists — must not rewrite it */
        return p.find(".wf-settings") == std::string::npos;
    };
    h.write_file = [&] (const std::string& path, const std::string& c) {
        written[path] = c;
        return true;
    };

    std::string err;
    ASSERT_TRUE(persist_output_to_kanshi("/tmp/kanshi/config", o, real, &h, &err)) << err;

    /* Managed sibling only */
    ASSERT_EQ(written.count("/tmp/kanshi/config.wf-settings"), 1u);
    EXPECT_EQ(written.count("/tmp/kanshi/config"), 0u);
    EXPECT_NE(written["/tmp/kanshi/config.wf-settings"].find("profile auto_HDMI-A-1"),
        std::string::npos);
    EXPECT_NE(written["/tmp/kanshi/config.wf-settings"].find("5120x1440@"), std::string::npos);
}

TEST(DisplayConfig, KanshiSeedsMainOnlyWhenMissing)
{
    DisplayOutput o;
    o.name = "eDP-1";
    DisplayMode real;
    real.width = 1920;
    real.height = 1080;
    real.refresh_hz = 60.0;
    o.modes.push_back(real);

    std::map<std::string, std::string> written;
    DisplayConfigHooks h;
    h.path_exists = [] (const std::string&) { return false; };
    h.write_file = [&] (const std::string& path, const std::string& c) {
        written[path] = c;
        return true;
    };

    std::string err;
    ASSERT_TRUE(persist_output_to_kanshi("/tmp/kanshi/config", o, real, &h, &err)) << err;
    EXPECT_EQ(written.count("/tmp/kanshi/config.wf-settings"), 1u);
    EXPECT_EQ(written.count("/tmp/kanshi/config"), 1u);
}

TEST(DisplayConfig, SupportsTolerance)
{
    DisplayOutput o;
    o.name = "HDMI-A-1";
    DisplayMode m;
    m.width = 5120;
    m.height = 1440;
    m.refresh_hz = 143.983994;
    o.modes.push_back(m);

    DisplayMode pick = m;
    pick.refresh_hz = 143.98; /* within 0.05 */
    EXPECT_TRUE(o.supports(pick));
    pick.refresh_hz = 140.0;
    EXPECT_FALSE(o.supports(pick));
}

TEST(DisplayConfig, SplitResolutionAndRefreshResponsive)
{
    DisplayOutput o;
    o.name = "HDMI-A-1";
    auto add = [&] (int w, int h, double hz) {
        DisplayMode m;
        m.width = w;
        m.height = h;
        m.refresh_hz = hz;
        o.modes.push_back(m);
    };
    add(5120, 1440, 143.983994);
    add(5120, 1440, 119.991997);
    add(5120, 1440, 59.995998);
    add(3840, 1080, 119.969);
    add(3840, 1080, 59.984);
    add(1920, 1080, 60.0);

    auto res = o.unique_resolutions();
    ASSERT_EQ(res.size(), 3u);
    EXPECT_EQ(res[0].first, 5120);
    EXPECT_EQ(res[0].second, 1440);

    auto rates_uw = o.refresh_rates_for(5120, 1440);
    ASSERT_EQ(rates_uw.size(), 3u);
    EXPECT_GT(rates_uw[0], rates_uw[1]); /* sorted desc */

    auto rates_fhd = o.refresh_rates_for(1920, 1080);
    ASSERT_EQ(rates_fhd.size(), 1u);

    /* Improper pair: 5120x1440 does not advertise 60.000 exact as only FHD */
    auto bad = o.resolve_safe(5120, 1440, 60.000); /* 59.995 is close — within 1Hz find */
    /* 60.0 vs 59.995 is within 1.0 find_mode tolerance */
    EXPECT_TRUE(bad.valid());

    auto impossible = o.resolve_safe(5120, 1440, 30.0);
    EXPECT_FALSE(impossible.valid());

    auto ok = o.resolve_safe(5120, 1440, 144.0);
    EXPECT_TRUE(ok.valid());
    EXPECT_TRUE(o.supports(ok));

    EXPECT_EQ(wf_shell::resolution_label(5120, 1440).find("5120"), 0u);
    EXPECT_NE(wf_shell::refresh_label(143.98).find("144"), std::string::npos);
}
