#pragma once

/**
 * StartupGate — TAOCP-style pure domain check for “is it safe to open Settings?”
 *
 * Side effects stay at the boundary (main / on_activate). This module only
 * answers questions from env + filesystem facts. Fail-soft: never throw;
 * never invent compositor state.
 *
 * Mother-test: if we cannot open safely, the app says so in plain language
 * and exits without touching the desktop.
 */

#include <string>
#include <vector>

namespace wf_settings
{

struct StartupGate
{
    bool wayland_display_set = false;
    bool wayland_socket_present = false;
    bool runtime_dir_ok = false;
    bool compositor_probe_ok = false; /* optional soft connect later */
    std::vector<std::string> warnings;
    std::vector<std::string> blockers; /* hard: do not open UI */

    bool safe_to_open() const
    {
        return blockers.empty() && wayland_display_set && wayland_socket_present;
    }

    /** One short status line for the user (plain English). */
    std::string user_summary() const;
};

/**
 * Pure-ish preflight: reads env + checks socket path existence.
 * Does not setenv, does not talk to the compositor protocol stack.
 */
StartupGate evaluate_startup_gate();

/**
 * Boundary: sanitize process env before Gtk::Application starts.
 * - Prefer existing WAYLAND_DISPLAY; never invent a nested session.
 * - Do not force GDK_BACKEND if the user left it unset (X11 fallback OK).
 * - Drop clearly dead DBUS_SESSION_BUS_ADDRESS (stale path → Connection refused).
 * - Never set DISPLAY solely so Settings can open (panel children need DISPLAY;
 *   a settings window must not depend on inventing one).
 *
 * Returns gate after sanitize (re-evaluated).
 */
StartupGate prepare_settings_process_env();

} // namespace wf_settings
