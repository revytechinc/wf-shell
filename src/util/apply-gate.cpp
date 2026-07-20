#include "apply-gate.hpp"
#include "gtk-utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace wf_shell
{

std::string ApplyGateResult::summary() const
{
    if (ok && warnings.empty())
    {
        return "ok";
    }
    std::ostringstream o;
    if (!ok)
    {
        o << "BLOCKED";
        for (const auto& b : blockers)
        {
            o << " | " << b;
        }
    } else
    {
        o << "ok-with-warnings";
    }
    for (const auto& w : warnings)
    {
        o << " | warn: " << w;
    }
    return o.str();
}

bool apply_debug_enabled()
{
    if (const char *e = std::getenv("WF_SHELL_DEBUG"); e && e[0] && e[0] != '0')
    {
        return true;
    }
    if (const char *e = std::getenv("WF_SETTINGS_DEBUG"); e && e[0] && e[0] != '0')
    {
        return true;
    }
    return false;
}

void gate_log(const char *scope, const std::string& msg)
{
    if (!apply_debug_enabled())
    {
        return;
    }
    std::cerr << "wf-shell:gate[" << (scope ? scope : "?") << "]: " << msg << "\n";
}

void gate_log_result(const char *scope, const ApplyGateResult& r)
{
    /* Blockers always log; full dump when debug on. */
    if (!r.ok)
    {
        std::cerr << "wf-shell:gate[" << (scope ? scope : "?")
                  << "]: REFUSE apply — " << r.summary() << "\n";
        return;
    }
    if (apply_debug_enabled())
    {
        std::cerr << "wf-shell:gate[" << (scope ? scope : "?")
                  << "]: ALLOW apply — " << r.summary() << "\n";
    }
}

ApplyGateResult validate_theme_css_path(const std::string& css_path)
{
    ApplyGateResult r;
    gate_log("theme-css", "validate path=\"" + css_path + "\"");

    if (css_path.empty())
    {
        r.ok = true;
        gate_log("theme-css", "empty path → default theme (ok)");
        return r;
    }

    auto check = check_css_file_path(css_path, false);
    if (!check.ok)
    {
        r.block(check.reason);
        gate_log_result("theme-css", r);
        return r;
    }

    /* Probe-load with GTK — never persist a path that cannot load. */
    auto probe = load_css_from_path(css_path);
    if (!probe)
    {
        r.block("CSS failed GTK load probe: " + css_path);
        gate_log_result("theme-css", r);
        return r;
    }

    r.ok = true;
    gate_log("theme-css", "probe-load ok, size validated for " + css_path);
    gate_log_result("theme-css", r);
    return r;
}

ApplyGateResult validate_theme_apply(const std::string& theme_id,
    const std::string& resolved_css_path)
{
    ApplyGateResult r;
    gate_log("theme-apply", "id=\"" + theme_id + "\" css=\"" + resolved_css_path + "\"");

    if (theme_id.empty())
    {
        r.block("theme id empty");
        gate_log_result("theme-apply", r);
        return r;
    }

    if (theme_id != "default" && resolved_css_path.empty())
    {
        r.block("non-default theme has empty css path: " + theme_id);
        gate_log_result("theme-apply", r);
        return r;
    }

    auto css_gate = validate_theme_css_path(resolved_css_path);
    if (!css_gate.ok)
    {
        for (const auto& b : css_gate.blockers)
        {
            r.block(b);
        }
        for (const auto& w : css_gate.warnings)
        {
            r.warn(w);
        }
        gate_log_result("theme-apply", r);
        return r;
    }
    for (const auto& w : css_gate.warnings)
    {
        r.warn(w);
    }

    r.ok = true;
    gate_log_result("theme-apply", r);
    return r;
}

ApplyGateResult validate_wayland_session_for_live_apply()
{
    ApplyGateResult r;
    const char *wd = std::getenv("WAYLAND_DISPLAY");
    const char *rd = std::getenv("XDG_RUNTIME_DIR");
    gate_log("wayland", std::string("WAYLAND_DISPLAY=") + (wd ? wd : "(unset)") +
        " XDG_RUNTIME_DIR=" + (rd ? rd : "(unset)"));

    if (!wd || !wd[0])
    {
        r.block("WAYLAND_DISPLAY unset — refuse live compositor apply");
        gate_log_result("wayland", r);
        return r;
    }
    if (!rd || !rd[0])
    {
        r.block("XDG_RUNTIME_DIR unset — refuse live compositor apply");
        gate_log_result("wayland", r);
        return r;
    }
    std::string sock = std::string(rd) + "/" + wd;
    std::error_code ec;
    if (!std::filesystem::exists(sock, ec) || ec)
    {
        r.block("Wayland socket missing: " + sock);
        gate_log_result("wayland", r);
        return r;
    }
    r.ok = true;
    gate_log_result("wayland", r);
    return r;
}

} // namespace wf_shell
