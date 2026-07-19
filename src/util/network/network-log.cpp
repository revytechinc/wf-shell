#include "network-log.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace wf_net
{
namespace
{

std::mutex g_log_mu;
std::string g_log_path;
bool g_path_ready = false;

std::string event_log_path()
{
    if (g_path_ready)
    {
        return g_log_path;
    }
    g_path_ready = true;
    const char *home = std::getenv("HOME");
    if (!home || !*home)
    {
        g_log_path.clear();
        return g_log_path;
    }
    /* Ensure directory exists (best-effort). */
    std::string dir = std::string(home) + "/.local/state/wf-shell";
    std::string mkdir_cmd = "mkdir -p '" + dir + "' 2>/dev/null";
    (void)std::system(mkdir_cmd.c_str());
    g_log_path = dir + "/network-events.jsonl";
    return g_log_path;
}

std::string iso_local_now(int64_t *ms_out)
{
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    if (ms_out)
    {
        *ms_out = ms;
    }
    std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    int frac = static_cast<int>(ms % 1000);
    char out[80];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, frac);
    return out;
}

} // namespace

std::string json_escape_string(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    o.push_back('"');
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", c);
                    o += hex;
                } else
                {
                    o.push_back(static_cast<char>(c));
                }
        }
    }
    o.push_back('"');
    return o;
}

std::vector<EventField> wifi_state_fields(const InterfaceInfo& info)
{
    std::vector<EventField> f;
    f.push_back(field_str("name", info.name));
    f.push_back(field_str("kind", kind_label(info.kind)));
    f.push_back(field_bool("up", info.up));
    f.push_back(field_bool("running", info.running));
    f.push_back(field_str("status", info.status));
    f.push_back(field_str("wifi_ssid", info.wifi_ssid));
    f.push_back(field_str("wifi_wpa_state", info.wifi_wpa_state));
    f.push_back(field_str("wifi_bssid", info.wifi_bssid));
    f.push_back(field_int("wifi_channel", info.wifi_channel));
    f.push_back(field_str("wifi_parent", info.wifi_parent));
    f.push_back(field_int("wifi_role", static_cast<int>(info.wifi_role)));
    f.push_back(field_int("wifi_signal_dbm", info.wifi_signal_dbm));
    f.push_back(field_int("wifi_signal_pct", info.wifi_signal_pct));
    f.push_back(field_str("conn_state", format_wifi_connection_state(info)));
    f.push_back(field_bool("default_route", info.is_default_route));
    f.push_back(field_bool("default_route_v4", info.is_default_route_v4));
    f.push_back(field_bool("default_route_v6", info.is_default_route_v6));
    f.push_back(field_str("icon", icon_for_interface(info)));
    f.push_back(field_str("ipv4", info.ipv4.empty() ? "" : info.ipv4.front()));
    f.push_back(field_str("ipv6", info.ipv6.empty() ? "" : info.ipv6.front()));
    f.push_back(field_int("ipv4_count", static_cast<long long>(info.ipv4.size())));
    f.push_back(field_int("ipv6_count", static_cast<long long>(info.ipv6.size())));
    f.push_back(field_str("media", info.media));
    return f;
}

void net_event(const char *type, const char *level,
    std::vector<EventField> data, const std::string& iface)
{
    if (!type || !*type)
    {
        type = "unknown";
    }
    if (!level || !*level)
    {
        level = "info";
    }

    int64_t ms = 0;
    std::string ts = iso_local_now(&ms);

    std::ostringstream oss;
    oss << '{'
        << "\"ts\":" << json_escape_string(ts) << ','
        << "\"ts_ms\":" << ms << ','
        << "\"src\":\"wf-panel.network\","
        << "\"type\":" << json_escape_string(type) << ','
        << "\"level\":" << json_escape_string(level);
    if (!iface.empty())
    {
        oss << ",\"iface\":" << json_escape_string(iface);
    }
    oss << ",\"data\":{";
    bool first = true;
    for (const auto& f : data)
    {
        if (f.key.empty())
        {
            continue;
        }
        if (!first)
        {
            oss << ',';
        }
        first = false;
        oss << json_escape_string(f.key) << ':';
        if (f.is_raw)
        {
            oss << (f.value.empty() ? "null" : f.value);
        } else
        {
            oss << json_escape_string(f.value);
        }
    }
    oss << "}}";

    const std::string line = oss.str();

    std::lock_guard<std::mutex> lock(g_log_mu);
    /* Always mirror to stderr for journal/console capture */
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);

    const std::string path = event_log_path();
    if (!path.empty())
    {
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line << '\n';
        }
    }
}

} // namespace wf_net
