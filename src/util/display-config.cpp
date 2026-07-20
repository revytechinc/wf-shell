#include "display-config.hpp"
#include "apply-gate.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <array>
#include <algorithm>
#include <iostream>
#include <sys/wait.h>

#include <wayfire/nonstd/json.hpp>

namespace wf_shell
{
namespace
{

std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
    {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
    {
        ++i;
    }
    return s.substr(i);
}

int run_popen(const std::string& cmd, std::string& out, std::string& err)
{
    out.clear();
    err.clear();
    std::string full = cmd + " 2>/tmp/wf-display-cmd.err";
    FILE *fp = popen(full.c_str(), "r");
    if (!fp)
    {
        err = "popen failed";
        return -1;
    }
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), buf.size(), fp))
    {
        out += buf.data();
    }
    int st = pclose(fp);
    std::ifstream efile("/tmp/wf-display-cmd.err");
    if (efile)
    {
        std::ostringstream oss;
        oss << efile.rdbuf();
        err = oss.str();
    }
    if (WIFEXITED(st))
    {
        return WEXITSTATUS(st);
    }
    return st == 0 ? 0 : -1;
}

std::string default_read_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        return {};
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

bool default_write_file(const std::string& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out << contents;
    return static_cast<bool>(out);
}

bool default_path_exists(const std::string& path)
{
    std::ifstream in(path);
    return static_cast<bool>(in);
}

std::string upsert_ini_section(const std::string& file, const std::string& section_header,
    const std::vector<std::string>& body_lines)
{
    std::istringstream in(file);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }

    std::vector<std::string> out;
    bool skipping = false;
    bool replaced = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& l = lines[i];
        if (l == section_header)
        {
            skipping = true;
            if (!replaced)
            {
                out.push_back(section_header);
                for (const auto& b : body_lines)
                {
                    out.push_back(b);
                }
                replaced = true;
            }
            continue;
        }
        if (skipping)
        {
            if (!l.empty() && l[0] == '[')
            {
                skipping = false;
                out.push_back(l);
            }
            continue;
        }
        out.push_back(l);
    }
    if (!replaced)
    {
        if (!out.empty() && !out.back().empty())
        {
            out.push_back("");
        }
        out.push_back(section_header);
        for (const auto& b : body_lines)
        {
            out.push_back(b);
        }
    }
    std::ostringstream oss;
    for (size_t i = 0; i < out.size(); ++i)
    {
        oss << out[i] << '\n';
    }
    return oss.str();
}

double member_double(const wf::json_reference_t& obj, const char *key, double def = 0.0)
{
    if (!obj.has_member(key))
    {
        return def;
    }
    auto v = obj[key];
    if (v.is_double())
    {
        return v.as_double();
    }
    if (v.is_int())
    {
        return static_cast<double>(v.as_int());
    }
    if (v.is_int64())
    {
        return static_cast<double>(v.as_int64());
    }
    return def;
}

int member_int(const wf::json_reference_t& obj, const char *key, int def = 0)
{
    if (!obj.has_member(key))
    {
        return def;
    }
    auto v = obj[key];
    if (v.is_int())
    {
        return v.as_int();
    }
    if (v.is_int64())
    {
        return static_cast<int>(v.as_int64());
    }
    if (v.is_double())
    {
        return static_cast<int>(v.as_double());
    }
    return def;
}

bool member_bool(const wf::json_reference_t& obj, const char *key, bool def = false)
{
    if (!obj.has_member(key))
    {
        return def;
    }
    auto v = obj[key];
    if (v.is_bool())
    {
        return v.as_bool();
    }
    if (v.is_string())
    {
        auto s = v.as_string();
        return s == "true" || s == "enabled" || s == "1";
    }
    return def;
}

std::string member_string(const wf::json_reference_t& obj, const char *key)
{
    if (!obj.has_member(key))
    {
        return {};
    }
    auto v = obj[key];
    if (v.is_string())
    {
        return v.as_string();
    }
    return {};
}

} // namespace

int refresh_hz_to_millihertz(double hz)
{
    if (hz <= 0.0)
    {
        return 0;
    }
    return static_cast<int>(std::lround(hz * 1000.0));
}

std::string DisplayMode::label() const
{
    if (!valid())
    {
        return "(invalid)";
    }
    int hz_round = static_cast<int>(std::lround(refresh_hz));
    std::ostringstream oss;
    oss << width << "\u00d7" << height << " @ " << hz_round << " Hz";
    if (preferred)
    {
        oss << " (preferred)";
    }
    if (current)
    {
        oss << " (current)";
    }
    return oss.str();
}

std::string DisplayMode::wlr_mode_arg() const
{
    std::ostringstream oss;
    oss << width << "x" << height << "@";
    /* Enough precision for wlr-randr to match the advertised mode. */
    oss.setf(std::ios::fixed);
    oss.precision(6);
    oss << refresh_hz;
    std::string s = oss.str();
    /* trim trailing zeros after decimal */
    if (s.find('.') != std::string::npos)
    {
        while (!s.empty() && s.back() == '0')
        {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.')
        {
            s.pop_back();
        }
    }
    return s;
}

std::string DisplayMode::wayfire_mode_string() const
{
    std::ostringstream oss;
    oss << width << "x" << height << "@" << refresh_hz_to_millihertz(refresh_hz);
    return oss.str();
}

DisplayMode DisplayOutput::current_mode() const
{
    for (const auto& m : modes)
    {
        if (m.current)
        {
            return m;
        }
    }
    return {};
}

DisplayMode DisplayOutput::preferred_mode() const
{
    for (const auto& m : modes)
    {
        if (m.preferred)
        {
            return m;
        }
    }
    return {};
}

bool DisplayOutput::supports(const DisplayMode& mode) const
{
    if (!mode.valid())
    {
        return false;
    }
    for (const auto& m : modes)
    {
        if (m.same_geometry(mode))
        {
            return true;
        }
    }
    return false;
}

DisplayMode DisplayOutput::find_mode(int w, int h, double hz) const
{
    DisplayMode best{};
    double best_d = 1e9;
    for (const auto& m : modes)
    {
        if (m.width != w || m.height != h)
        {
            continue;
        }
        double d = std::abs(m.refresh_hz - hz);
        if (d < best_d)
        {
            best_d = d;
            best   = m;
        }
    }
    if (best.valid() && best_d < 1.0)
    {
        return best;
    }
    return {};
}

std::vector<std::pair<int, int>> DisplayOutput::unique_resolutions() const
{
    std::vector<std::pair<int, int>> out;
    for (const auto& m : modes)
    {
        std::pair<int, int> r{m.width, m.height};
        if (std::find(out.begin(), out.end(), r) == out.end())
        {
            out.push_back(r);
        }
    }
    std::sort(out.begin(), out.end(), [] (const auto& a, const auto& b) {
        long pa = (long)a.first * a.second;
        long pb = (long)b.first * b.second;
        if (pa != pb)
        {
            return pa > pb;
        }
        return a.first > b.first;
    });
    return out;
}

std::vector<double> DisplayOutput::refresh_rates_for(int w, int h) const
{
    std::vector<double> rates;
    for (const auto& m : modes)
    {
        if (m.width != w || m.height != h)
        {
            continue;
        }
        bool dup = false;
        for (double r : rates)
        {
            if (std::abs(r - m.refresh_hz) < 0.05)
            {
                dup = true;
                break;
            }
        }
        if (!dup)
        {
            rates.push_back(m.refresh_hz);
        }
    }
    std::sort(rates.begin(), rates.end(), std::greater<double>());
    return rates;
}

DisplayMode DisplayOutput::resolve_safe(int w, int h, double hz) const
{
    return find_mode(w, h, hz);
}

std::string resolution_label(int w, int h)
{
    return std::to_string(w) + "\u00d7" + std::to_string(h);
}

std::string refresh_label(double hz)
{
    int r = static_cast<int>(std::lround(hz));
    return std::to_string(r) + " Hz";
}

DisplayConfigHooks default_display_config_hooks()
{
    DisplayConfigHooks h;
    h.run_cmd = [] (const std::string& cmd, std::string& o, std::string& e) {
        return run_popen(cmd, o, e);
    };
    h.read_file   = default_read_file;
    h.write_file  = default_write_file;
    h.path_exists = default_path_exists;
    return h;
}

DisplayProbeResult parse_wlr_randr_json(const std::string& json_text)
{
    DisplayProbeResult r;
    if (json_text.empty())
    {
        r.warnings.push_back("empty wlr-randr json");
        return r;
    }
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err)
    {
        r.warnings.push_back("json parse: " + *err);
        return r;
    }
    if (!root.is_array())
    {
        r.warnings.push_back("wlr-randr json root is not an array");
        return r;
    }
    const size_t n = root.size();
    for (size_t i = 0; i < n; ++i)
    {
        auto jo = root[i];
        if (!jo.is_object())
        {
            continue;
        }
        DisplayOutput o;
        o.name        = member_string(jo, "name");
        o.description = member_string(jo, "description");
        o.make        = member_string(jo, "make");
        o.model       = member_string(jo, "model");
        o.serial      = member_string(jo, "serial");
        o.enabled     = member_bool(jo, "enabled", true);
        if (jo.has_member("physical_size") && jo["physical_size"].is_object())
        {
            o.physical_w_mm = member_int(jo["physical_size"], "width");
            o.physical_h_mm = member_int(jo["physical_size"], "height");
        }
        if (jo.has_member("position") && jo["position"].is_object())
        {
            o.pos_x = member_int(jo["position"], "x");
            o.pos_y = member_int(jo["position"], "y");
        }
        o.scale = member_double(jo, "scale", 1.0);
        o.transform = member_string(jo, "transform");
        if (o.transform.empty())
        {
            o.transform = "normal";
        }
        if (jo.has_member("adaptive_sync"))
        {
            o.adaptive_sync = member_bool(jo, "adaptive_sync", false);
        }
        if (jo.has_member("modes") && jo["modes"].is_array())
        {
            auto jmodes = jo["modes"];
            for (size_t mi = 0; mi < jmodes.size(); ++mi)
            {
                auto jm = jmodes[mi];
                if (!jm.is_object())
                {
                    continue;
                }
                DisplayMode m;
                m.width      = member_int(jm, "width");
                m.height     = member_int(jm, "height");
                m.refresh_hz = member_double(jm, "refresh");
                m.preferred  = member_bool(jm, "preferred");
                m.current    = member_bool(jm, "current");
                if (m.valid())
                {
                    o.modes.push_back(m);
                }
            }
        }
        std::sort(o.modes.begin(), o.modes.end(), [] (const DisplayMode& a, const DisplayMode& b) {
            if (a.current != b.current)
            {
                return a.current > b.current;
            }
            long pa = (long)a.width * a.height;
            long pb = (long)b.width * b.height;
            if (pa != pb)
            {
                return pa > pb;
            }
            return a.refresh_hz > b.refresh_hz;
        });
        if (!o.name.empty())
        {
            r.outputs.push_back(std::move(o));
        }
    }
    r.ok = !r.outputs.empty();
    if (!r.ok)
    {
        r.warnings.push_back("no outputs discovered");
    }
    return r;
}

DisplayProbeResult probe_displays(DisplayConfigHooks *hooks_in)
{
    DisplayProbeResult r;
    DisplayConfigHooks local;
    DisplayConfigHooks *hooks = hooks_in;
    if (!hooks)
    {
        local = default_display_config_hooks();
        hooks = &local;
    }
    if (!hooks->run_cmd)
    {
        r.warnings.push_back("no run_cmd hook");
        return r;
    }

    /* Preflight: refuse to talk to a missing compositor (avoids hang/crash). */
    const char *wd = std::getenv("WAYLAND_DISPLAY");
    const char *rd = std::getenv("XDG_RUNTIME_DIR");
    if (!wd || !wd[0])
    {
        r.warnings.push_back("WAYLAND_DISPLAY unset — not probing (safe)");
        return r;
    }
    if (rd && rd[0])
    {
        std::string sock = std::string(rd) + "/" + wd;
        if (!default_path_exists(sock))
        {
            r.warnings.push_back("Wayland socket missing (" + sock +
                ") — not probing (safe)");
            return r;
        }
    }

    std::string out, err;
    /* Hard timeout so a wedged wlr-randr cannot hang Settings forever.
     * Prefer system timeout(1); fall back to bare command. */
    const char *probe_cmd =
        "command -v timeout >/dev/null 2>&1 && "
        "timeout 3 wlr-randr --json || wlr-randr --json";
    int code = hooks->run_cmd(probe_cmd, out, err);
    if (code != 0)
    {
        r.warnings.push_back("wlr-randr --json failed (exit " + std::to_string(code) +
            "): " + trim(err));
        return r;
    }
    return parse_wlr_randr_json(out);
}

bool apply_display_mode(const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks_in, std::string *error)
{
    gate_log("display_apply", "begin output=" + output.name + " mode=" + mode.label());

    /*
     * Pure domain gate first (always). Session/tool gates only for real OS
     * hooks — unit tests inject hooks_in and must not require a live seat.
     */
    if (!mode.valid())
    {
        if (error)
        {
            *error = "mode invalid";
        }
        gate_log("display_apply", "REFUSE invalid mode");
        return false;
    }
    if (output.name.empty())
    {
        if (error)
        {
            *error = "output name empty";
        }
        return false;
    }
    if (!output.supports(mode))
    {
        if (error)
        {
            *error = "mode not advertised by " + output.name + ": " + mode.label() +
                " (refusing unsafe mode)";
        }
        gate_log("display_apply", "REFUSE mode not in hardware list");
        return false;
    }

    DisplayConfigHooks local;
    DisplayConfigHooks *hooks = hooks_in;
    if (!hooks)
    {
        auto sess = validate_wayland_session_for_live_apply();
        if (!sess.ok)
        {
            if (error)
            {
                *error = sess.summary();
            }
            gate_log_result("display_apply", sess);
            return false;
        }
        /* wlr-randr must exist before we touch the live compositor */
        if (system("command -v wlr-randr >/dev/null 2>&1") != 0)
        {
            if (error)
            {
                *error = "wlr-randr not installed — refuse display apply";
            }
            gate_log("display_apply", "REFUSE wlr-randr missing");
            return false;
        }
        local = default_display_config_hooks();
        hooks = &local;
    }
    std::ostringstream cmd;
    cmd << "wlr-randr --output " << output.name
        << " --mode " << mode.wlr_mode_arg()
        << " --pos " << output.pos_x << "," << output.pos_y
        << " --scale " << output.scale
        << " --transform " << (output.transform.empty() ? "normal" : output.transform);
    if (output.adaptive_sync)
    {
        cmd << " --adaptive-sync enabled";
    }
    gate_log("display_apply", "cmd: " + cmd.str());
    std::string out, err;
    int code = hooks->run_cmd(cmd.str(), out, err);
    if (code != 0)
    {
        if (error)
        {
            *error = "wlr-randr apply failed: " + trim(err.empty() ? out : err);
        }
        gate_log("display_apply", "cmd failed exit=" + std::to_string(code));
        return false;
    }
    gate_log("display_apply", "ok");
    return true;
}

bool persist_output_to_wayfire_ini(const std::string& ini_path,
    const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks_in, std::string *error)
{
    if (!output.supports(mode))
    {
        if (error)
        {
            *error = "refusing to persist unadvertised mode for " + output.name;
        }
        return false;
    }
    DisplayConfigHooks local;
    DisplayConfigHooks *hooks = hooks_in;
    if (!hooks)
    {
        local = default_display_config_hooks();
        hooks = &local;
    }
    std::string existing;
    if (hooks->read_file)
    {
        existing = hooks->read_file(ini_path);
    }
    std::string header = "[output:" + output.name + "]";
    std::ostringstream scale_s;
    scale_s << output.scale;
    std::vector<std::string> body = {
        "mode = " + mode.wayfire_mode_string(),
        "position = " + std::to_string(output.pos_x) + "," + std::to_string(output.pos_y),
        "transform = " + (output.transform.empty() ? "normal" : output.transform),
        "scale = " + scale_s.str(),
        std::string("vrr = ") + (output.adaptive_sync ? "true" : "false"),
    };
    std::string next = upsert_ini_section(existing, header, body);
    if (!hooks->write_file || !hooks->write_file(ini_path, next))
    {
        if (error)
        {
            *error = "failed to write " + ini_path;
        }
        return false;
    }
    return true;
}

bool persist_output_to_kanshi(const std::string& kanshi_path,
    const DisplayOutput& output, const DisplayMode& mode,
    DisplayConfigHooks *hooks_in, std::string *error)
{
    if (!output.supports(mode))
    {
        if (error)
        {
            *error = "refusing kanshi profile for unadvertised mode";
        }
        return false;
    }
    DisplayConfigHooks local;
    DisplayConfigHooks *hooks = hooks_in;
    if (!hooks)
    {
        local = default_display_config_hooks();
        hooks = &local;
    }
    /*
     * Never clobber the whole kanshi config (multi-monitor profiles).
     * Append / replace only our managed profile block if present; otherwise
     * leave existing profiles and write a sibling managed file.
     */
    std::string managed = kanshi_path + ".wf-settings";
    std::ostringstream oss;
    oss << "# Auto-generated by wf-settings — safe to include from kanshi/config.\n";
    oss << "# Only modes reported by the compositor/EDID are used.\n";
    oss << "profile auto_" << output.name << " {\n";
    if (!output.description.empty())
    {
        oss << "  output \"" << output.description << "\" mode "
            << mode.wlr_mode_arg() << " position " << output.pos_x << "," << output.pos_y
            << " scale " << output.scale << "\n";
    }
    oss << "  output " << output.name << " mode " << mode.wlr_mode_arg()
        << " position " << output.pos_x << "," << output.pos_y
        << " scale " << output.scale << "\n";
    oss << "}\n";
    if (!hooks->write_file || !hooks->write_file(managed, oss.str()))
    {
        if (error)
        {
            *error = "failed to write " + managed;
        }
        return false;
    }
    /* If main config is missing, seed a minimal include-friendly file once. */
    if (hooks->path_exists && !hooks->path_exists(kanshi_path))
    {
        std::string seed = "# Managed profile also in " + managed + "\n" + oss.str();
        (void)hooks->write_file(kanshi_path, seed);
    }
    return true;
}

bool apply_and_persist_display(const DisplayOutput& output, const DisplayMode& mode,
    const std::string& wayfire_ini_path,
    const std::string& kanshi_path,
    DisplayConfigHooks *hooks, std::string *error)
{
    if (!apply_display_mode(output, mode, hooks, error))
    {
        return false;
    }
    if (!wayfire_ini_path.empty())
    {
        if (!persist_output_to_wayfire_ini(wayfire_ini_path, output, mode, hooks, error))
        {
            return false;
        }
    }
    if (!kanshi_path.empty())
    {
        std::string kerr;
        (void)persist_output_to_kanshi(kanshi_path, output, mode, hooks, &kerr);
    }
    return true;
}

} // namespace wf_shell
