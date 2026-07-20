#pragma once

/**
 * Apply wf-shell theme CSS to wf-settings (same pack as the panel).
 * Loads default.css + panel/css_path from config.json (wins) or wf-shell.ini.
 */

namespace wf_settings
{

/** Drop previous providers and load current theme onto the default display. */
void reload_app_theme();

} // namespace wf_settings
