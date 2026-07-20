#include "glibmm/ustring.h"
#include <gtk-utils.hpp>
#include <glibmm.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/csssection.h>
#include <gdk/gdkcairo.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <giomm/desktopappinfo.h>
#include "wf-shell-app.hpp"

Glib::RefPtr<Gdk::Pixbuf> load_icon_pixbuf_safe(std::string icon_path, int size)
{
    try {
        auto pb = Gdk::Pixbuf::create_from_file(icon_path, size, size);
        return pb;
    } catch (Glib::FileError&)
    {
        std::cerr << "Error loading file: " << icon_path << std::endl;
        return {};
    } catch (Gdk::PixbufError&)
    {
        std::cerr << "Pixbuf error: " << icon_path << std::endl;
        return {};
    } catch (...)
    {
        std::cerr << "Failed to load: " << icon_path << std::endl;
        return {};
    }
}

namespace
{
/* Hard cap — theme CSS should never need multi-MB files. */
constexpr std::uintmax_t kMaxCssBytes = 2 * 1024 * 1024;
} // namespace

CssPathCheck check_css_file_path(const std::string& path, bool allow_empty)
{
    CssPathCheck r;
    if (path.empty())
    {
        r.ok = allow_empty;
        r.reason = allow_empty ? "" : "empty css path";
        return r;
    }
    std::error_code ec;
    namespace fs = std::filesystem;
    if (!fs::exists(path, ec) || ec)
    {
        r.reason = "css path does not exist: " + path;
        return r;
    }
    if (!fs::is_regular_file(path, ec) || ec)
    {
        r.reason = "css path is not a regular file: " + path;
        return r;
    }
    auto sz = fs::file_size(path, ec);
    if (ec)
    {
        r.reason = "cannot stat css file: " + path;
        return r;
    }
    if (sz == 0)
    {
        r.reason = "css file is empty: " + path;
        return r;
    }
    if (sz > kMaxCssBytes)
    {
        r.reason = "css file too large (" + std::to_string(sz) + " bytes): " + path;
        return r;
    }
    /* Readable? */
    std::ifstream in(path);
    if (!in)
    {
        r.reason = "css file not readable: " + path;
        return r;
    }
    r.ok = true;
    return r;
}

Glib::RefPtr<Gtk::CssProvider> load_css_from_path(std::string path)
{
    auto check = check_css_file_path(path, false);
    if (!check.ok)
    {
        std::cerr << "wf-shell: skip CSS — " << check.reason << std::endl;
        return {};
    }
    try {
        auto css = Gtk::CssProvider::create();
        int parse_errors = 0;
        css->signal_parsing_error().connect(
            [&parse_errors, &path] (
                const Glib::RefPtr<const Gtk::CssSection>& /* section */,
                const Glib::Error& err) {
                ++parse_errors;
                std::cerr << "wf-shell: CSS parse warning in " << path
                          << ": " << err.what() << std::endl;
            });
        css->load_from_path(path);
        if (parse_errors > 0)
        {
            std::cerr << "wf-shell: loaded " << path << " with " << parse_errors
                      << " parse warning(s); applying partial styles"
                      << std::endl;
        }
        return css;
    } catch (Glib::Error& err)
    {
        std::cerr << "wf-shell: failed to load CSS " << path << ": "
                  << err.what() << std::endl;
        return {};
    } catch (...)
    {
        std::cerr << "wf-shell: failed to load CSS at: " << path << std::endl;
        return {};
    }
}

Glib::RefPtr<Gtk::CssProvider> load_css_from_string_safe(const std::string& css_text,
    std::string *error)
{
    try {
        auto css = Gtk::CssProvider::create();
        int parse_errors = 0;
        css->signal_parsing_error().connect(
            [&parse_errors] (
                const Glib::RefPtr<const Gtk::CssSection>& /* section */,
                const Glib::Error& err) {
                ++parse_errors;
                std::cerr << "wf-shell: CSS string parse warning: "
                          << err.what() << std::endl;
            });
        css->load_from_string(css_text);
        if (parse_errors > 0 && error)
        {
            *error = "css string had " + std::to_string(parse_errors) + " parse warning(s)";
        }
        return css;
    } catch (Glib::Error& err)
    {
        if (error)
        {
            *error = err.what();
        }
        std::cerr << "wf-shell: failed to load CSS string: " << err.what()
                  << std::endl;
        return {};
    } catch (...)
    {
        if (error)
        {
            *error = "unknown error loading css string";
        }
        std::cerr << "wf-shell: failed to load CSS string\n";
        return {};
    }
}

void invert_pixbuf(Glib::RefPtr<Gdk::Pixbuf>& pbuff)
{
    int channels = pbuff->get_n_channels();
    int stride   = pbuff->get_rowstride();

    auto data = pbuff->get_pixels();
    int w     = pbuff->get_width();
    int h     = pbuff->get_height();

    for (int i = 0; i < w; i++)
    {
        for (int j = 0; j < h; j++)
        {
            auto p = data + j * stride + i * channels;
            p[0] = 255 - p[0];
            p[1] = 255 - p[1];
            p[2] = 255 - p[2];
        }
    }
}

namespace IconProvider
{
std::map<std::string, std::string> custom_icons;

std::string tolower(std::string str)
{
    for (auto& c : str)
    {
        c = std::tolower(c);
    }

    return str;
}

/* Gio::DesktopAppInfo
 *
 * Usually knowing the app_id, we can get a desktop app info from Gio
 * The filename is either the app_id + ".desktop" or lower_app_id + ".desktop" */
Glib::RefPtr<Gio::Icon> get_from_desktop_app_info(std::string app_id)
{
    std::vector<std::string> prefixes = {
        "",
        "org.kde.",
    };

    std::vector<std::string> app_id_variations = {
        app_id,
        tolower(app_id),
    };

    std::vector<std::string> suffixes = {
        "",
        ".desktop"
    };

    for (auto& prefix : prefixes)
    {
        for (auto& id : app_id_variations)
        {
            for (auto& suffix : suffixes)
            {
                auto app_info = Gio::DesktopAppInfo::create(prefix + id + suffix);
                if (app_info)
                {
                    return app_info->get_icon();
                }
            }
        }
    }

    return {};
}

bool image_set_icon(Gtk::Image & image, Glib::ustring path)
{
    if (path.empty())
    {
        return false;
    }

    if ((path.rfind("/", 0) == 0) || (path.rfind("~", 0) == 0))
    {
        image.set(path);
        return true;
    } else
    {
        /* It might be a space delimited list */
        std::istringstream stream(path);
        std::string app_id;
        auto theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
        while (stream >> app_id)
        {
            if (custom_icons.count(app_id) > 0)
            {
                image.set_from_icon_name(custom_icons[app_id]);
                return true;
            }

            /* If the icon theme has this, use it */
            if (theme && theme->has_icon(app_id))
            {
                image.set_from_icon_name(app_id);
                return true;
            }

            /* Might be appid, get the .desktop file */
            auto icon = get_from_desktop_app_info(app_id);
            if (icon)
            {
                image.set_from_icon_name(icon->to_string());
                return true;
            }
        }
    }

    return false;
}

void load_custom_icons(std::string section_name)
{
    static const std::string prefix = "icon_mapping_";
    auto section = WayfireShellApp::get().config.get_section(section_name);

    for (auto option : section->get_registered_options())
    {
        if (option->get_name().compare(0, prefix.length(), prefix) != 0)
        {
            continue;
        }

        auto app_id = option->get_name().substr(prefix.length());
        custom_icons[app_id] = option->get_value_str();
    }
}
}
