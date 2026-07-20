#pragma once

/**
 * ApplyGate — validate-before-apply (always).
 *
 * RULE (REVYTECH / user permanent): Never write config or touch the live
 * compositor/panel without a successful preflight. Fail-soft: refuse + log.
 *
 * Debug: set WF_SHELL_DEBUG=1 (or WF_SETTINGS_DEBUG=1) for verbose gate logs.
 */

#include <string>
#include <vector>

namespace wf_shell
{

/** Structured validation result — never throw from gates. */
struct ApplyGateResult
{
    bool ok = false;
    std::vector<std::string> blockers; /* hard refuse */
    std::vector<std::string> warnings; /* may proceed with caution */

    void block(std::string msg)
    {
        blockers.push_back(std::move(msg));
        ok = false;
    }

    void warn(std::string msg)
    {
        warnings.push_back(std::move(msg));
    }

    void mark_ok_if_clean()
    {
        ok = blockers.empty();
    }

    std::string summary() const;
};

bool apply_debug_enabled();

/** Detail log line (always prefix wf-shell:gate:). Quiet unless debug on. */
void gate_log(const char *scope, const std::string& msg);

/** Always log blockers/warnings for a gate decision. */
void gate_log_result(const char *scope, const ApplyGateResult& r);

/**
 * Validate a theme CSS path before persisting or loading into the panel.
 * Empty path = default theme (ok).
 * Checks: exists, regular file, size, readable, GTK can load (probe).
 */
ApplyGateResult validate_theme_css_path(const std::string& css_path);

/**
 * Validate theme id + resolve path from packs before write.
 */
ApplyGateResult validate_theme_apply(const std::string& theme_id,
    const std::string& resolved_css_path);

/**
 * Compositor/session gate for live display modeset.
 */
ApplyGateResult validate_wayland_session_for_live_apply();

} // namespace wf_shell
