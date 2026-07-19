#pragma once

/**
 * Structured network event log — one JSON object per line (JSONL).
 *
 * Each event:
 *   {
 *     "ts":   "<ISO-8601 local>",
 *     "ts_ms": <unix_ms>,
 *     "src":  "wf-panel.network",
 *     "type": "<event type>",
 *     "level":"info|warn|error|debug",
 *     "iface":"wlan0",          // optional
 *     "data": { ... }           // type-specific fields
 *   }
 *
 * Emit to stderr and append to ~/.local/state/wf-shell/network-events.jsonl
 * when possible. Fail-soft, never throws. Thread-safe.
 */

#include "network-types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wf_net
{

/** String field for event data object. */
struct EventField
{
    std::string key;
    std::string value; /**< already a JSON value fragment, or use helpers */
    bool is_raw = false; /**< true → value is raw JSON (number/bool/object) */
};

inline EventField field_str(const char *k, const std::string& v)
{
    return EventField{k, v, false};
}

inline EventField field_raw(const char *k, const std::string& json_value)
{
    return EventField{k, json_value, true};
}

inline EventField field_int(const char *k, long long n)
{
    return EventField{k, std::to_string(n), true};
}

inline EventField field_bool(const char *k, bool b)
{
    return EventField{k, b ? "true" : "false", true};
}

/** Emit one structured event. level: debug|info|warn|error */
void net_event(const char *type, const char *level,
    std::vector<EventField> data = {},
    const std::string& iface = {});

/** Convenience: info level */
inline void net_event_info(const char *type, std::vector<EventField> data = {},
    const std::string& iface = {})
{
    net_event(type, "info", std::move(data), iface);
}

inline void net_event_warn(const char *type, std::vector<EventField> data = {},
    const std::string& iface = {})
{
    net_event(type, "warn", std::move(data), iface);
}

inline void net_event_error(const char *type, std::vector<EventField> data = {},
    const std::string& iface = {})
{
    net_event(type, "error", std::move(data), iface);
}

inline void net_event_debug(const char *type, std::vector<EventField> data = {},
    const std::string& iface = {})
{
    net_event(type, "debug", std::move(data), iface);
}

/** Snapshot key Wi‑Fi fields into event data (does not emit). */
std::vector<EventField> wifi_state_fields(const InterfaceInfo& info);

/** Escape a string for JSON (quotes + content). Pure. */
std::string json_escape_string(const std::string& s);

} // namespace wf_net
