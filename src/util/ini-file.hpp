#pragma once

/**
 * Minimal INI section/key helpers for settings apply/save.
 * Used by wf-settings pages (not a full config parser).
 */

#include <map>
#include <string>
#include <vector>

namespace wf_shell
{

/** Read key under [section]; empty if missing. */
std::string ini_get(const std::string& path, const std::string& section,
    const std::string& key);

/** Set key under [section] (creates section if needed). Removes duplicate keys. */
bool ini_set(const std::string& path, const std::string& section,
    const std::string& key, const std::string& value);

/** Batch set many keys in one section (single read/write). */
bool ini_set_many(const std::string& path, const std::string& section,
    const std::map<std::string, std::string>& kv);

bool ini_get_bool(const std::string& path, const std::string& section,
    const std::string& key, bool default_val = false);

int ini_get_int(const std::string& path, const std::string& section,
    const std::string& key, int default_val = 0);

} // namespace wf_shell
