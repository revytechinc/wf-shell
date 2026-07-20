#pragma once

/**
 * Theme pack discovery + panel/css_path persistence.
 * Pure-ish: FS scan via hooks-optional defaults; used by panel menu (legacy)
 * and wf-settings Panel page.
 */

#include <map>
#include <string>
#include <vector>

namespace wf_shell
{

struct ThemePack
{
    std::string id;
    std::string name;
    std::string path; /* empty for default */
    std::string era;  /* modern | retro | "" */
};

/**
 * Discover .css packs under install themes dir + ~/.config/wf-shell/themes.
 * Always includes "default" with empty path.
 */
std::map<std::string, ThemePack> discover_theme_packs(
    const std::string& resource_themes_dir,
    const std::string& user_themes_dir);

/** Ordered list for UI: default, modern…, other…, retro… (alpha within). */
std::vector<ThemePack> theme_packs_ui_order(
    const std::map<std::string, ThemePack>& packs);

/** Read last non-empty panel/css_path from ini. */
std::string get_ini_css_path(const std::string& ini_path);

/** Write single css_path under [panel], drop duplicates. */
bool update_ini_css_path(const std::string& ini_path, const std::string& css_path);

/**
 * Apply theme id: resolve path, write ini. Returns false if id unknown
 * (except "default" which clears css_path).
 */
bool apply_theme_pack(const std::string& theme_id,
    const std::string& ini_path,
    const std::string& resource_themes_dir,
    const std::string& user_themes_dir,
    std::string *error = nullptr);

} // namespace wf_shell
