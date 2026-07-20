#pragma once

/**
 * Display configuration — auto-discover what the hardware/compositor reports,
 * apply only advertised modes, and persist to wayfire.ini (and optionally kanshi).
 *
 * Source of truth for "what can we set?" is the live session (wlr-randr / wlr
 * output management), NOT free-form user strings. Never invent modes.
 *
 * Pure parsing/formatting is unit-tested; apply/persist use injectable hooks.
 */

#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wf_shell
{

struct DisplayMode
{
    int width  = 0;
    int height = 0;
    /** Refresh rate in Hz as reported by the compositor (e.g. 143.983994). */
    double refresh_hz = 0.0;
    bool preferred = false;
    bool current   = false;

    bool valid() const
    {
        return width > 0 && height > 0 && refresh_hz > 0.0;
    }

    /** Human label: "5120×1440 @ 144 Hz" (rounded Hz for display). */
    std::string label() const;

    /** wlr-randr --mode argument: "5120x1440@143.983994" */
    std::string wlr_mode_arg() const;

    /**
     * wayfire.ini mode value: "5120x1440@143984" (refresh in millihertz).
     * @see wayfire wiki / example wayfire.ini
     */
    std::string wayfire_mode_string() const;

    bool same_geometry(const DisplayMode& o) const
    {
        return width == o.width && height == o.height &&
            std::abs(refresh_hz - o.refresh_hz) < 0.05;
    }
};

struct DisplayOutput
{
    std::string name; /* connector, e.g. HDMI-A-1 */
    std::string description;
    std::string make;
    std::string model;
    std::string serial;
    int physical_w_mm = 0;
    int physical_h_mm = 0;
    bool enabled = true;
    int pos_x = 0;
    int pos_y = 0;
    double scale = 1.0;
    std::string transform = "normal"; /* normal|90|180|270|… */
    bool adaptive_sync = false;
    std::vector<DisplayMode> modes;

    DisplayMode current_mode() const;
    DisplayMode preferred_mode() const;

    /** True if mode matches an advertised mode (geometry + refresh tolerance). */
    bool supports(const DisplayMode& mode) const;

    /** Find closest advertised mode or invalid mode if none. */
    DisplayMode find_mode(int w, int h, double hz) const;

    /** Unique WxH pairs (sorted pixels desc). Labels like "5120×1440". */
    std::vector<std::pair<int, int>> unique_resolutions() const;

    /**
     * All advertised refresh rates for exact WxH (sorted desc).
     * Empty if resolution is not advertised.
     */
    std::vector<double> refresh_rates_for(int w, int h) const;

    /**
     * Resolve WxH + preferred Hz to an advertised mode (within tolerance).
     * Invalid if that pair is not supported.
     */
    DisplayMode resolve_safe(int w, int h, double hz) const;
};

/** Label for resolution dropdown: "5120×1440". */
std::string resolution_label(int w, int h);

/** Label for refresh dropdown: "144 Hz" (rounded). */
std::string refresh_label(double hz);

struct DisplayProbeResult
{
    std::vector<DisplayOutput> outputs;
    std::vector<std::string> warnings;
    bool ok = false;
};

struct DisplayConfigHooks
{
    /** Run command, capture stdout; return exit code. */
    std::function<int(const std::string& cmd, std::string& stdout_out, std::string& stderr_out)> run_cmd;
    std::function<std::string(const std::string& path)> read_file;
    std::function<bool(const std::string& path, const std::string& contents)> write_file;
    std::function<bool(const std::string& path)> path_exists;
};

/** Default hooks: popen-style command, file IO. */
DisplayConfigHooks default_display_config_hooks();

/**
 * Parse `wlr-randr --json` document into outputs/modes.
 * Pure: no process spawn.
 */
DisplayProbeResult parse_wlr_randr_json(const std::string& json_text);

/**
 * Auto-discover connected outputs and every mode the compositor reports.
 * Uses `wlr-randr --json` (requires WAYLAND_DISPLAY session).
 */
DisplayProbeResult probe_displays(DisplayConfigHooks *hooks = nullptr);

/**
 * Apply a mode that must already be in output.modes (safety).
 * Runs: wlr-randr --output NAME --mode WxH@Hz --pos x,y --scale S --transform T
 */
bool apply_display_mode(const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks = nullptr, std::string *error = nullptr);

/**
 * Upsert [output:NAME] in wayfire.ini with mode/position/scale/transform.
 * Only writes modes that pass output.supports(mode).
 */
bool persist_output_to_wayfire_ini(const std::string& ini_path,
    const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks = nullptr, std::string *error = nullptr);

/**
 * Write a simple kanshi profile for this single-output setup (optional companion
 * to wayfire.ini when autostart runs kanshi).
 */
bool persist_output_to_kanshi(const std::string& kanshi_path,
    const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks = nullptr, std::string *error = nullptr);

/**
 * Apply + persist to wayfire.ini (+ kanshi if path non-empty).
 * Refuses unknown modes.
 */
bool apply_and_persist_display(const DisplayOutput& output, const DisplayMode& mode,
    const std::string& wayfire_ini_path,
    const std::string& kanshi_path = {},
    DisplayConfigHooks *hooks = nullptr, std::string *error = nullptr);

/** Refresh mHz for wayfire (143.983994 → 143984). */
int refresh_hz_to_millihertz(double hz);

} // namespace wf_shell
