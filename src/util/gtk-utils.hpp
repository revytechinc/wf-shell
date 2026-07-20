#pragma once

#include "glibmm/ustring.h"
#include <gtkmm/image.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/cssprovider.h>
#include <string>

/* Loads a pixbuf with the given size from the given file, returns null if unsuccessful */
Glib::RefPtr<Gdk::Pixbuf> load_icon_pixbuf_safe(std::string icon_path, int size);

/**
 * Pre-flight check for a theme/CSS file path (no GTK parse).
 * Empty path is "ok" only if allow_empty (caller skips load).
 * Rejects missing, non-files, unreadable, oversized blobs.
 */
struct CssPathCheck
{
    bool ok = false;
    std::string reason;
};

CssPathCheck check_css_file_path(const std::string& path,
    bool allow_empty = false);

/**
 * Load CSS from path. On failure returns null (caller must ignore / fall back).
 * Never throws. Logs parse warnings; still returns provider if GTK accepted the
 * file (partial apply is GTK's model). Missing/unreadable → null.
 */
Glib::RefPtr<Gtk::CssProvider> load_css_from_path(std::string path);

/**
 * load_from_string with try/catch — null on hard failure so config-driven CSS
 * (fonts, colors) cannot take down the panel.
 */
Glib::RefPtr<Gtk::CssProvider> load_css_from_string_safe(const std::string& css,
    std::string *error = nullptr);

struct WfIconLoadOptions
{
    int user_scale = -1;
    bool invert    = false;
};

namespace IconProvider
{
void invert_pixbuf(Glib::RefPtr<Gdk::Pixbuf>& pbuff);
bool image_set_icon(Gtk::Image & image, Glib::ustring path);
void load_custom_icons(std::string section_name);
}
