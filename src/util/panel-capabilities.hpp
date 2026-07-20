#pragma once

/**
 * What the *built* panel can actually host — never present UI for absent widgets.
 *
 * Settings, config sanitizers, and tests share this list so catalogs cannot
 * advertise weather/volume/etc. when the binary was built without them.
 */

#include <string>
#include <vector>

namespace wf_shell
{

/**
 * Widget ids the current build can construct (mirrors panel.cpp widget_from_name
 * feature gates). Spacers/separators always available.
 */
std::vector<std::string> available_panel_widget_ids();

/** True if name is constructible in this build (spacingN / separatorN ok). */
bool panel_widget_available(const std::string& name);

/**
 * Drop unknown / unbuilt widget tokens from a widgets_left|center|right string.
 * Empty result becomes "none".
 */
std::string sanitize_panel_widgets_list(const std::string& list);

/**
 * Theme pack is presentable only if CSS exists *and* menu icon artifact exists
 * (or theme is default). Missing icon → do not list in Settings.
 */
bool theme_artifacts_complete(const std::string& theme_id,
    const std::string& css_path,
    const std::string& resource_icons_dir,
    const std::string& user_menu_icons_dir = {});

/** Resolve menu icon path for theme pack id (empty if missing). */
std::string resolve_theme_menu_icon_path(const std::string& menu_icon_id,
    const std::string& resource_icons_dir,
    const std::string& user_menu_icons_dir = {});

} // namespace wf_shell
