#pragma once

#include <string>
#include <vector>

/**
 * Pure theme / menu-icon defaults — no GTK, no filesystem required for the
 * core map and id rules. Path helpers accept optional roots for tests.
 */
namespace wf_shell
{

/** Empty css_path (or unreadable) ⇒ "default". Else file stem (e.g. crt-phosphor). */
std::string theme_id_from_css_path(const std::string& css_path);

/**
 * Menu button pack id for a theme.
 * "default" (and unknown) ⇒ "wayfire" (classic mark).
 */
std::string theme_default_menu_icon_id(const std::string& theme_id);

/** True when theme_id represents the built-in default (no custom css_path). */
bool is_default_theme_id(const std::string& theme_id);

/**
 * UI state after applying a theme selection (theme picker).
 * For "default", css_path is empty and menu_icon_id is "wayfire".
 */
struct ThemeSelectionState
{
    std::string theme_id;
    std::string css_path;      /* empty for default */
    std::string menu_icon_id;  /* pack id or "wayfire" */
};

/** Select a theme by id (as the menu theme combo would). */
ThemeSelectionState select_theme(const std::string& theme_id,
    const std::string& themes_install_dir = {});

/**
 * Clear custom theme → defaults (css_path empty, wayfire menu icon).
 * Same as select_theme("default").
 */
ThemeSelectionState clear_theme_to_defaults();

/**
 * After select then clear, verify invariants:
 *  - theme_id == "default"
 *  - css_path empty
 *  - menu_icon_id == "wayfire"
 */
bool theme_defaults_are_restored(const ThemeSelectionState& state);

/**
 * Candidate icon tokens for a theme (for menu button load order).
 * Does not touch the filesystem unless pack_root / wayfire_file are used
 * by the caller; this returns logical ids/names for pure tests.
 *
 * Order: theme pack id, "wayfire", then symbolic/stock names.
 */
std::vector<std::string> menu_icon_logical_candidates(const std::string& theme_id);

/** Known theme ids that ship a dedicated pack icon (excludes default). */
std::vector<std::string> themed_menu_icon_theme_ids();

} // namespace wf_shell
