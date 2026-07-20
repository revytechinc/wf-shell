#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <cassert>
#include <cctype>
#include <cstring>
#include <giomm.h>
#include <glibmm/spawn.h>
#include <iostream>
#include <cstdlib>
#include <gtk4-layer-shell.h>

extern char **environ;

#include "menu.hpp"
#include "gtk-utils.hpp"
#include "wf-autohide-window.hpp"
#include "wf-popover.hpp"
#include "power-controller.hpp"
#include "platform.hpp"
#include "wf-shell-app.hpp"
#include "theme-defaults.hpp"
#include "session-env.hpp"
#include <gdkmm/pixbuf.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

const std::string default_icon = "wayfire";

namespace
{

struct ThemeEntry
{
    std::string id;
    std::string name;
    std::string path; /* empty for default */
    std::string era;  /* modern | retro | "" */
};

std::string trim_copy(std::string s)
{
    auto not_space = [] (unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string title_case_id(const std::string& id)
{
    std::string out;
    bool cap = true;
    for (char c : id)
    {
        if (c == '-' || c == '_')
        {
            out.push_back(' ');
            cap = true;
            continue;
        }
        if (cap)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            cap = false;
        } else
        {
            out.push_back(c);
        }
    }
    return out;
}

/* Parse optional wf-shell-theme header: id=...; name=...; era=... */
void parse_theme_header(const std::string& path, ThemeEntry& te)
{
    std::ifstream in(path);
    if (!in)
    {
        return;
    }
    std::string line;
    for (int i = 0; i < 5 && std::getline(in, line); ++i)
    {
        auto pos = line.find("wf-shell-theme:");
        if (pos == std::string::npos)
        {
            continue;
        }
        auto rest = line.substr(pos + 15);
        auto take = [&] (const char *key) -> std::string {
            auto k = std::string(key) + "=";
            auto p = rest.find(k);
            if (p == std::string::npos)
            {
                return {};
            }
            p += k.size();
            auto end = rest.find_first_of(";,*/", p);
            auto val = rest.substr(p, end == std::string::npos ? std::string::npos : end - p);
            return trim_copy(val);
        };
        auto id = take("id");
        auto name = take("name");
        auto era = take("era");
        if (!id.empty())
        {
            te.id = id;
        }
        if (!name.empty())
        {
            te.name = name;
        }
        if (!era.empty())
        {
            te.era = era;
        }
        break;
    }
}

std::string home_dir()
{
    const char *home = getenv("HOME");
    return home ? std::string(home) : std::string{};
}

std::string ini_path()
{
    auto h = home_dir();
    return h.empty() ? std::string{} : h + "/.config/wf-shell.ini";
}

/** Last non-empty panel/css_path from ini (handles duplicates). */
std::string get_ini_css_path()
{
    std::string path = ini_path();
    if (path.empty())
    {
        return {};
    }
    std::ifstream in(path);
    if (!in)
    {
        return {};
    }

    std::string line;
    std::string last;
    bool in_panel = false;
    while (std::getline(in, line))
    {
        std::string trimmed = trim_copy(line);
        if (trimmed == "[panel]")
        {
            in_panel = true;
            continue;
        }
        if (!trimmed.empty() && trimmed.front() == '[')
        {
            in_panel = false;
            continue;
        }
        if (in_panel && trimmed.rfind("css_path", 0) == 0)
        {
            auto eq = trimmed.find('=');
            if (eq != std::string::npos)
            {
                last = trim_copy(trimmed.substr(eq + 1));
            }
        }
    }
    return last;
}

/** Write a single css_path under [panel], removing duplicates elsewhere. */
void update_ini_css_path(const std::string& path)
{
    std::string ip = ini_path();
    if (ip.empty())
    {
        return;
    }

    std::ifstream in(ip);
    std::vector<std::string> lines;
    std::string line;
    bool in_panel = false;
    bool found_css_path = false;
    bool saw_panel = false;

    if (in)
    {
        while (std::getline(in, line))
        {
            std::string trimmed = trim_copy(line);
            if (trimmed == "[panel]")
            {
                in_panel = true;
                saw_panel = true;
                lines.push_back(line);
                continue;
            }
            if (!trimmed.empty() && trimmed.front() == '[')
            {
                in_panel = false;
                lines.push_back(line);
                continue;
            }
            /* Drop all css_path lines; re-insert one under [panel]. */
            if (trimmed.rfind("css_path", 0) == 0)
            {
                if (in_panel && !found_css_path)
                {
                    lines.push_back("css_path = " + path);
                    found_css_path = true;
                }
                continue;
            }
            lines.push_back(line);
        }
        in.close();
    }

    if (!saw_panel)
    {
        lines.push_back("[panel]");
        lines.push_back("css_path = " + path);
        found_css_path = true;
    } else if (!found_css_path)
    {
        for (auto it = lines.begin(); it != lines.end(); ++it)
        {
            if (trim_copy(*it) == "[panel]")
            {
                lines.insert(it + 1, "css_path = " + path);
                break;
            }
        }
    }

    std::ofstream out(ip);
    if (!out)
    {
        return;
    }
    for (const auto& l : lines)
    {
        out << l << "\n";
    }
}

/** Discover installed + user themes. Map id → ThemeEntry (user overrides system). */
std::map<std::string, ThemeEntry> discover_themes()
{
    std::map<std::string, ThemeEntry> themes;
    themes["default"] = ThemeEntry{"default", "Default Theme", "", ""};

    auto scan_dir = [&] (const std::string& dir, bool user) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
        {
            return;
        }
        for (auto& p : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec || !p.is_regular_file())
            {
                continue;
            }
            if (p.path().extension() != ".css")
            {
                continue;
            }
            ThemeEntry te;
            te.id   = p.path().stem().string();
            te.name = title_case_id(te.id);
            te.path = p.path().string();
            parse_theme_header(te.path, te);
            if (te.id.empty())
            {
                te.id = p.path().stem().string();
            }
            if (te.name.empty())
            {
                te.name = title_case_id(te.id);
            }
            /* User themes win by id. */
            if (user || themes.find(te.id) == themes.end() || te.id == "default")
            {
                if (te.id != "default")
                {
                    themes[te.id] = te;
                }
            }
        }
    };

    scan_dir(std::string(RESOURCEDIR) + "/themes", false);
    auto h = home_dir();
    if (!h.empty())
    {
        scan_dir(h + "/.config/wf-shell/themes", true);
    }
    return themes;
}

std::string resolve_theme_path(const std::string& id)
{
    if (id.empty() || id == "default")
    {
        return {};
    }
    auto themes = discover_themes();
    auto it = themes.find(id);
    if (it != themes.end())
    {
        return it->second.path;
    }
    return {};
}

/* ── Menu button icons (theme pack under ICONDIR/menu) ─────────────────── */

#ifndef ICONDIR
#define ICONDIR RESOURCEDIR "/icons"
#endif

std::string menu_icons_dir()
{
    return std::string(ICONDIR) + "/menu";
}

std::string user_menu_icons_dir()
{
    auto h = home_dir();
    return h.empty() ? std::string{} : h + "/.config/wf-shell/menu-icons";
}

using wf_shell::theme_id_from_css_path;
using wf_shell::theme_default_menu_icon_id;

/** Classic Wayfire panel mark — same asset the menu used before theming. */
std::string wayfire_menu_icon_file()
{
    std::error_code ec;
    const std::string candidates[] = {
        std::string(ICONDIR) + "/wayfire.png",
        std::string(ICONDIR) + "/wayfire.svg",
        std::string(RESOURCEDIR) + "/icons/wayfire.png",
        std::string(RESOURCEDIR) + "/icons/scalable/wayfire.svg",
        std::string(RESOURCEDIR) + "/wayfire.png",
        /* hicolor installs from meson */
        home_dir() + "/.local/share/icons/hicolor/scalable/apps/wayfire.svg",
        home_dir() + "/.local/share/icons/hicolor/48x48/apps/wayfire.png",
        "/usr/local/share/icons/hicolor/scalable/apps/wayfire.svg",
        "/usr/local/share/icons/hicolor/48x48/apps/wayfire.png",
        "/usr/share/icons/hicolor/scalable/apps/wayfire.svg",
    };
    for (const auto& p : candidates)
    {
        if (!p.empty() && std::filesystem::is_regular_file(p, ec))
        {
            return p;
        }
    }
    return {};
}

std::string pack_icon_path(const std::string& id)
{
    if (id.empty() || id == "auto")
    {
        return {};
    }
    if (id == "wayfire")
    {
        return wayfire_menu_icon_file();
    }
    std::error_code ec;
    const std::string dirs[] = {
        menu_icons_dir(),
        user_menu_icons_dir(),
        std::string(RESOURCEDIR) + "/icons/menu",
    };
    for (const auto& dir : dirs)
    {
        if (dir.empty())
        {
            continue;
        }
        for (const char *ext : {".svg", ".png", ".svgz"})
        {
            std::string p = dir + "/" + id + ext;
            if (std::filesystem::is_regular_file(p, ec))
            {
                return p;
            }
        }
    }
    return {};
}

std::string current_theme_id_live()
{
    std::string css;
    try
    {
        css = static_cast<std::string>(WfOption<std::string>{"panel/css_path"});
    } catch (...)
    {
        css = get_ini_css_path();
    }
    if (css.empty())
    {
        css = get_ini_css_path();
    }
    return theme_id_from_css_path(css);
}

/**
 * Load menu icon from file or icon-theme name.
 * IconProvider::image_set_icon() returns true for absolute paths even when
 * the load fails (blank image) — so we load files ourselves and only then
 * fall through a safe default chain.
 */
bool set_menu_button_icon(Gtk::Image& image, const std::string& path_or_name, int px = 32)
{
    if (path_or_name.empty())
    {
        return false;
    }

    std::string path = path_or_name;
    if (!path.empty() && path[0] == '~')
    {
        auto h = home_dir();
        if (!h.empty())
        {
            path = h + path.substr(1);
        }
    }

    if (!path.empty() && path[0] == '/')
    {
        try
        {
            auto pb = Gdk::Pixbuf::create_from_file(path, px, px, true);
            if (pb)
            {
                image.set(pb);
                return true;
            }
        } catch (...)
        {}
        try
        {
            /* gtkmm4: Image::set(filename) */
            image.set(path);
            return true;
        } catch (...)
        {}
        return false;
    }

    /* Icon theme / app-id name */
    if (IconProvider::image_set_icon(image, path_or_name))
    {
        return true;
    }
    /* has_icon() is sometimes wrong; still try the name (classic "wayfire"). */
    try
    {
        image.set_from_icon_name(path_or_name);
        return true;
    } catch (...)
    {}
    return false;
}

/** Filesystem-backed candidates: pack path first, then logical name fallbacks. */
std::vector<std::string> menu_icon_candidates_for_theme()
{
    std::vector<std::string> out;
    const std::string tid = current_theme_id_live();

    for (const auto& logical : wf_shell::menu_icon_logical_candidates(tid))
    {
        auto pack = pack_icon_path(logical);
        if (!pack.empty())
        {
            /* Prefer real file over bare name when we have one */
            bool have = false;
            for (auto& x : out)
            {
                if (x == pack)
                {
                    have = true;
                    break;
                }
            }
            if (!have)
            {
                out.push_back(pack);
            }
        }
        bool have_name = false;
        for (auto& x : out)
        {
            if (x == logical)
            {
                have_name = true;
                break;
            }
        }
        if (!have_name)
        {
            out.push_back(logical);
        }
    }

    /* Absolute Wayfire asset always early for default */
    if (wf_shell::is_default_theme_id(tid))
    {
        auto wf = wayfire_menu_icon_file();
        if (!wf.empty())
        {
            out.insert(out.begin(), wf);
        }
    }
    return out;
}

/** Populate ComboBoxText with current selection first, then the rest. */
void fill_combo_current_first(Gtk::ComboBoxText& combo,
    const std::vector<std::pair<std::string, std::string>>& items,
    const std::string& current_id)
{
    combo.remove_all();
    std::string cur = current_id;
    bool have_cur = false;
    for (const auto& it : items)
    {
        if (it.first == cur)
        {
            have_cur = true;
            break;
        }
    }
    if (!have_cur && !items.empty())
    {
        cur = items.front().first;
    }

    /* Current first */
    for (const auto& it : items)
    {
        if (it.first == cur)
        {
            combo.append(it.first, it.second);
            break;
        }
    }
    for (const auto& it : items)
    {
        if (it.first != cur)
        {
            combo.append(it.first, it.second);
        }
    }
    if (!items.empty())
    {
        combo.set_active_id(cur);
    }
}

} // namespace

WfMenuLayout::WfMenuLayout(WayfireMenu *menu) : menu(menu)
{}

void WfMenuLayout::allocate_vfunc(const Gtk::Widget& widget, int width, int height, int baseline)
{
    if (menu == nullptr)
    {
        return;
    }

    bool is_top = panel_position.value() == WF_WINDOW_POSITION_TOP;

    Gtk::Widget::Measurements entry_measurements, logout_measurements, separator_measurements;
    entry_measurements     = menu->search_entry.measure(Gtk::Orientation::VERTICAL, width);
    logout_measurements    = menu->box_bottom.measure(Gtk::Orientation::VERTICAL, width);
    separator_measurements = menu->separator.measure(Gtk::Orientation::VERTICAL, width);

    /*
     * Always reserve footer (theme + logout) and search first so logout is
     * never clipped out of view when the popover is short or wide.
     */
    const int footer_h = logout_measurements.sizes.minimum;
    const int sep_h    = separator_measurements.sizes.natural;
    const int search_h = entry_measurements.sizes.minimum;
    int remaining_height = height - (search_h + footer_h + sep_h);
    if (remaining_height < 0)
    {
        remaining_height = 0;
    }

    search_alloc.set_x(0);
    search_alloc.set_y(is_top ? 0 : height - (footer_h + sep_h + search_h));
    search_alloc.set_height(search_h);
    search_alloc.set_width(width);
    menu->search_entry.size_allocate(search_alloc, -1);

    logout_alloc.set_x(0);
    logout_alloc.set_y(height - footer_h);
    logout_alloc.set_width(width);
    logout_alloc.set_height(footer_h);
    menu->box_bottom.size_allocate(logout_alloc, -1);

    separator_alloc.set_x(0);
    separator_alloc.set_y(height - (footer_h + sep_h));
    separator_alloc.set_width(width);
    separator_alloc.set_height(sep_h);
    menu->separator.size_allocate(separator_alloc, -1);

    if (show_categories.value())
    {
        category_alloc.set_x(0);
        category_alloc.set_y(is_top ? entry_measurements.sizes.minimum : 0);
        category_alloc.set_width(category_width);
        category_alloc.set_height(remaining_height);
        menu->category_scrolled_window.size_allocate(category_alloc, -1);

        flow_alloc.set_x(category_width);
        flow_alloc.set_y(is_top ? entry_measurements.sizes.minimum : 0);
        flow_alloc.set_width(width - category_width);
        flow_alloc.set_height(remaining_height);
        menu->app_scrolled_window.size_allocate(flow_alloc, -1);
    } else
    {
        /* Even if we're not having it, allocate some space */
        category_alloc.set_x(0);
        category_alloc.set_y(is_top ? entry_measurements.sizes.minimum : 0);
        category_alloc.set_width(width);
        category_alloc.set_height(remaining_height);
        menu->category_scrolled_window.size_allocate(category_alloc, -1);

        flow_alloc.set_x(0);
        flow_alloc.set_y(is_top ? entry_measurements.sizes.minimum : 0);
        flow_alloc.set_width(width);
        flow_alloc.set_height(remaining_height);
        menu->app_scrolled_window.size_allocate(flow_alloc, -1);
    }
}

void WfMenuLayout::measure_vfunc(const Gtk::Widget& widget, Gtk::Orientation orientation,
    int for_size, int& minimum, int& natural, int& minimum_baseline,
    int& natural_baseline) const
{
    minimum_baseline = -1;
    natural_baseline = -1;
    // What is our preferred width?
    if (orientation == Gtk::Orientation::HORIZONTAL)
    {
        if (limit_width > 0)
        {
            minimum = limit_width;
            natural = limit_width;
            return;
        }

        minimum = category_width + content_width;
        natural = category_width + content_width;
    } else
    {
        if (limit_height > 0)
        {
            minimum = limit_height;
            natural = limit_height;
            return;
        }

        Gtk::Widget::Measurements entry_measurements, logout_measurements, separator_measurements;
        entry_measurements     = menu->search_entry.measure(Gtk::Orientation::VERTICAL, for_size);
        logout_measurements    = menu->box_bottom.measure(Gtk::Orientation::VERTICAL, for_size);
        separator_measurements = menu->separator.measure(Gtk::Orientation::VERTICAL, for_size);

        minimum = separator_measurements.sizes.natural + entry_measurements.sizes.minimum +
            logout_measurements.sizes.minimum + content_height;
        natural = separator_measurements.sizes.natural + entry_measurements.sizes.minimum +
            logout_measurements.sizes.minimum + content_height;
    }
}

void WfMenuLayout::set_limit(int w, int h)
{
    if ((w == limit_width) && (h == limit_height))
    {
        return;
    }

    limit_width  = w;
    limit_height = h;
    menu->popover_layout_box.queue_resize();
}

WfMenuCategory::WfMenuCategory(std::string _name, std::string _icon_name) :
    name(_name), icon_name(_icon_name)
{}

std::string WfMenuCategory::get_name()
{
    return name;
}

std::string WfMenuCategory::get_icon_name()
{
    return icon_name;
}

WfMenuCategoryButton::WfMenuCategoryButton(WayfireMenu *_menu, std::string _category, std::string _label,
    std::string _icon_name) :
    Gtk::Button(), menu(_menu), category(_category), label(_label), icon_name(_icon_name)
{
    m_image.set_from_icon_name(icon_name);
    m_image.set_pixel_size(32);
    m_label.set_text(label);
    m_label.set_xalign(0.0);
    m_label.add_css_class("default-icon");


    m_box.append(m_image);
    m_box.append(m_label);
    m_box.set_homogeneous(false);

    this->set_child(m_box);
    this->add_css_class("flat");
    this->add_css_class("app-category");

    sig_click = this->signal_clicked().connect(
        sigc::mem_fun(*this, &WfMenuCategoryButton::on_click));
}

WfMenuCategoryButton::~WfMenuCategoryButton()
{
    sig_click.disconnect();
}

void WfMenuCategoryButton::on_click()
{
    menu->set_category(category);
}

/** Log launch diagnostics to stderr and a state-dir file (panel often has no TTY). */
static void menu_launch_log(const std::string& msg)
{
    std::cerr << "wf-panel: " << msg << std::endl;
    try
    {
        const char *state = std::getenv("XDG_STATE_HOME");
        const char *home  = std::getenv("HOME");
        std::string path;
        if (state && state[0])
        {
            path = std::string(state) + "/wayfire/menu-launch.log";
        } else if (home && home[0])
        {
            path = std::string(home) + "/.local/state/wayfire/menu-launch.log";
        } else
        {
            return;
        }
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << msg << "\n";
        }
    } catch (...)
    {}
}

/** Inject session/graphics env into Gio launch context — never edit .desktop files. */
static void inject_launch_env(const Glib::RefPtr<Gio::AppLaunchContext>& ctx)
{
    if (!ctx)
    {
        return;
    }
    try
    {
        auto env = wf_shell::session_env_for_app_launch(nullptr);
        for (const auto& kv : env)
        {
            if (!kv.second.empty())
            {
                ctx->setenv(kv.first, kv.second);
            }
        }
    } catch (const std::exception& e)
    {
        menu_launch_log(std::string("launch env inject failed: ") + e.what());
    } catch (...)
    {}
}

/**
 * Build a clean envp for app launch.
 *
 * Do NOT dump the full panel environ: when wf-panel is restarted from SSH/agent
 * tooling it carries TERM=dumb, GROK_*, SSH_*, NO_COLOR, etc. GUI apps inherit
 * that and can exit or fail to map a window — looks like "click does nothing".
 *
 * Start from a allowlist of session keys, then overlay session_env_for_app_launch.
 * Caller must g_strfreev the result.
 */
static char **build_launch_envp()
{
    static const char *keep[] = {
        "HOME", "USER", "LOGNAME", "SHELL", "PATH", "LANG", "LANGUAGE", "LC_ALL",
        "LC_CTYPE", "LC_MESSAGES", "LC_TIME", "TZ", "XAUTHORITY",
        "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME",
        "XDG_STATE_HOME", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS",
        "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "XDG_SESSION_DESKTOP",
        "XDG_SESSION_CLASS", "DESKTOP_SESSION",
        "DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET",
        "DBUS_SESSION_BUS_ADDRESS", "DBUS_SESSION_BUS_PID",
        "GDK_BACKEND", "GTK_THEME", "QT_QPA_PLATFORM", "QT_QPA_PLATFORMTHEME",
        "SDL_VIDEODRIVER", "CLUTTER_BACKEND", "MOZ_ENABLE_WAYLAND",
        "ELECTRON_OZONE_PLATFORM_HINT", "_JAVA_AWT_WM_NONREPARENTING",
        "PULSE_SERVER", "PULSE_RUNTIME_PATH", "PULSE_COOKIE",
        "LIBGL_DRI3_DISABLE", "__GLX_VENDOR_LIBRARY_NAME", "WLR_NO_HARDWARE_CURSORS",
        "LIBSEAT_BACKEND", "XDG_VTNR", "XDG_SEAT", "BRAVE_PATH", "BRAVE_WRAPPER",
        nullptr
    };

    std::map<std::string, std::string> merged;
    for (int i = 0; keep[i]; ++i)
    {
        const char *v = std::getenv(keep[i]);
        if (v && v[0])
        {
            merged[keep[i]] = v;
        }
    }

    /* Sensible TERM for anything that looks at it (never "dumb" from agent). */
    const char *term = std::getenv("TERM");
    if (!term || !term[0] || std::string(term) == "dumb" ||
        std::string(term) == "unknown")
    {
        merged["TERM"] = "xterm-256color";
    } else
    {
        merged["TERM"] = term;
    }

    auto overrides = wf_shell::session_env_for_app_launch(nullptr);
    for (const auto& kv : overrides)
    {
        if (!kv.second.empty())
        {
            merged[kv.first] = kv.second;
        }
    }

    /* Prefer real session toolkit defaults when still unset. */
    if (!merged.count("GDK_BACKEND") && merged.count("WAYLAND_DISPLAY"))
    {
        merged["GDK_BACKEND"] = "wayland";
    }
    if (!merged.count("QT_QPA_PLATFORM") && merged.count("WAYLAND_DISPLAY"))
    {
        merged["QT_QPA_PLATFORM"] = "wayland";
    }
    if (!merged.count("XDG_SESSION_TYPE") && merged.count("WAYLAND_DISPLAY"))
    {
        merged["XDG_SESSION_TYPE"] = "wayland";
    }
    /* FreeBSD Brave/Chromium port: avoid DRI3 glitches (matches brave-browser wrapper). */
    if (!merged.count("LIBGL_DRI3_DISABLE"))
    {
        merged["LIBGL_DRI3_DISABLE"] = "1";
    }

    char **envp = g_new0(char *, merged.size() + 1);
    size_t i = 0;
    for (const auto& kv : merged)
    {
        envp[i++] = g_strdup((kv.first + "=" + kv.second).c_str());
    }
    return envp;
}

/** Drop stale Chromium/Brave SingletonLock when the lock PID is dead. */
static void clear_stale_chromium_singleton(const std::string& config_sub)
{
    const char *home = std::getenv("HOME");
    if (!home || !home[0])
    {
        return;
    }
    std::string base = std::string(home) + "/.config/" + config_sub;
    std::string lock = base + "/SingletonLock";
    struct stat st{};
    if (lstat(lock.c_str(), &st) != 0)
    {
        return;
    }
    char buf[512];
    ssize_t n = readlink(lock.c_str(), buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        return;
    }
    buf[n] = '\0';
    /* Format: hostname-pid */
    std::string target(buf);
    auto dash = target.rfind('-');
    if (dash == std::string::npos)
    {
        return;
    }
    pid_t pid = static_cast<pid_t>(std::atoi(target.c_str() + dash + 1));
    if (pid <= 1)
    {
        return;
    }
    if (kill(pid, 0) == 0)
    {
        return; /* owner still alive */
    }
    menu_launch_log("clearing stale singleton for dead pid " + std::to_string(pid) +
        " in " + config_sub);
    unlink(lock.c_str());
    unlink((base + "/SingletonCookie").c_str());
    /* Socket path is often under /tmp; best-effort leave it */
}

static void pre_launch_app_cleanup(const Glib::RefPtr<Gio::DesktopAppInfo>& app)
{
    if (!app)
    {
        return;
    }
    std::string exe = app->get_executable();
    std::string name = app->get_name();
    std::string low = exe + " " + name;
    for (char& c : low)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (low.find("brave") != std::string::npos)
    {
        clear_stale_chromium_singleton("BraveSoftware/Brave-Browser");
    } else if (low.find("chrom") != std::string::npos)
    {
        clear_stale_chromium_singleton("chromium");
        clear_stale_chromium_singleton("google-chrome");
        clear_stale_chromium_singleton("chromium-browser");
    }
}

static void launch_child_setup(gpointer)
{
    /* New session so children outlive the panel cleanly. */
    setsid();
}

/**
 * Fallback when Gio::DesktopAppInfo::launch fails or is a no-op: spawn the
 * desktop Exec line with a full session environment. Never edits .desktop files.
 */
static bool spawn_desktop_app_fallback(const Glib::RefPtr<Gio::DesktopAppInfo>& app)
{
    if (!app)
    {
        return false;
    }
    /* Prefer the raw commandline (may contain %u etc.); strip field codes. */
    std::string cmd = app->get_commandline();
    if (cmd.empty())
    {
        std::string exe = app->get_executable();
        if (exe.empty())
        {
            return false;
        }
        cmd = exe;
    }
    /* Desktop entry field codes: strip for bare launch with no files/URIs. */
    static const char *codes[] = {
        "%f", "%F", "%u", "%U", "%i", "%c", "%k", "%d", "%D", "%n", "%N", "%v", "%m", nullptr
    };
    for (int i = 0; codes[i]; ++i)
    {
        for (size_t pos = 0; (pos = cmd.find(codes[i], pos)) != std::string::npos;)
        {
            cmd.erase(pos, std::strlen(codes[i]));
        }
    }
    /* Collapse whitespace */
    std::string cleaned;
    bool space = false;
    for (char c : cmd)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            space = true;
            continue;
        }
        if (space && !cleaned.empty())
        {
            cleaned.push_back(' ');
        }
        space = false;
        cleaned.push_back(c);
    }
    cmd = cleaned;
    if (cmd.empty())
    {
        return false;
    }

    /*
     * Chromium/Brave FreeBSD notes (spawn-time only — never edit .desktop):
     *
     * - The brave-browser wrapper adds --test-type (to hide the yellow
     *   "unsupported flag" banner). That also breaks profile picker / window
     *   creation ("No browser window found for startup"). Call the binary
     *   directly without --test-type.
     * - --no-sandbox --no-zygote are required on FreeBSD (no Linux user
     *   namespaces). The yellow "no sandbox" banner is expected and harmless.
     * - DRM/Wayland needs --render-node-override or ozone x11 via XWayland.
     */
    {
        std::string low = cmd;
        for (char& c : low)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const bool is_brave =
            low.find("brave") != std::string::npos;
        const bool is_chromium_family =
            is_brave ||
            low.find("chrom") != std::string::npos ||
            low.find("/chrome") != std::string::npos;

        if (is_brave)
        {
            /*
             * Preserve any trailing URL/args after the executable; rebuild the
             * front of the command without the port wrapper's --test-type.
             */
            std::string args;
            auto sp = cmd.find(' ');
            if (sp != std::string::npos)
            {
                args = cmd.substr(sp); /* includes leading space */
            }
            /* Drop flags we re-add or that break UI */
            auto strip_flag = [] (std::string& s, const char *flag)
            {
                for (;;)
                {
                    auto p = s.find(flag);
                    if (p == std::string::npos)
                    {
                        break;
                    }
                    auto e = p + std::strlen(flag);
                    while (e < s.size() && s[e] != ' ')
                    {
                        /* consume =value if present without space */
                        if (s[e] == '=')
                        {
                            while (e < s.size() && s[e] != ' ')
                            {
                                ++e;
                            }
                            break;
                        }
                        ++e;
                    }
                    s.erase(p, e - p);
                }
            };
            strip_flag(args, "--test-type");
            strip_flag(args, "--v=0");
            strip_flag(args, "--no-sandbox");
            strip_flag(args, "--no-zygote");

            std::string bin = "/usr/local/share/brave/brave";
            struct stat st{};
            if (stat(bin.c_str(), &st) != 0)
            {
                bin = "brave";
            }
            cmd = bin + " --no-sandbox --no-zygote" + args;
            menu_launch_log("brave: direct binary (no --test-type; --no-sandbox required on FreeBSD)");
        }

        if (is_chromium_family)
        {
            low = cmd;
            for (char& c : low)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const bool has_ozone = low.find("ozone-platform") != std::string::npos;
            const bool has_render = low.find("render-node-override") != std::string::npos;
            std::string extra;
            if (!has_ozone)
            {
                /*
                 * Prefer XWayland for Brave profile UI stability; Chrome can
                 * use wayland+render-node. DISPLAY is always injected.
                 */
                if (is_brave && std::getenv("DISPLAY") && std::getenv("DISPLAY")[0])
                {
                    extra += " --ozone-platform=x11";
                } else if (std::getenv("WAYLAND_DISPLAY") &&
                    std::getenv("WAYLAND_DISPLAY")[0])
                {
                    extra += " --ozone-platform=wayland";
                } else if (std::getenv("DISPLAY") && std::getenv("DISPLAY")[0])
                {
                    extra += " --ozone-platform=x11";
                }
            }
            if (!has_render && low.find("ozone-platform=x11") == std::string::npos &&
                extra.find("ozone-platform=x11") == std::string::npos)
            {
                struct stat st{};
                if (stat("/dev/dri/renderD128", &st) == 0)
                {
                    extra += " --render-node-override=/dev/dri/renderD128";
                }
            }
            if (!extra.empty())
            {
                cmd += extra;
                menu_launch_log("chromium flags:" + extra);
            }
        }
    }

    /* Terminal=true desktop entries need a terminal emulator. */
    bool needs_term = false;
    {
        auto path = app->get_filename();
        if (!path.empty())
        {
            GKeyFile *kf = g_key_file_new();
            GError *kerr = nullptr;
            if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, &kerr))
            {
                needs_term = g_key_file_get_boolean(kf, "Desktop Entry", "Terminal", nullptr);
            }
            g_clear_error(&kerr);
            g_key_file_free(kf);
        }
    }
    if (needs_term)
    {
        const char *terms[] = {
            "alacritty", "kitty", "xfce4-terminal", "gnome-terminal",
            "kgx", "xterm", nullptr
        };
        std::string wrapper;
        for (int i = 0; terms[i]; ++i)
        {
            gchar *found = g_find_program_in_path(terms[i]);
            if (!found)
            {
                continue;
            }
            g_free(found);
            if (std::string(terms[i]) == "alacritty" ||
                std::string(terms[i]) == "kitty" ||
                std::string(terms[i]) == "xterm" ||
                std::string(terms[i]) == "xfce4-terminal")
            {
                wrapper = std::string(terms[i]) + " -e " + cmd;
            } else
            {
                wrapper = std::string(terms[i]) + " -- " + cmd;
            }
            break;
        }
        if (wrapper.empty())
        {
            menu_launch_log("no terminal emulator found for " + app->get_name());
            return false;
        }
        cmd = wrapper;
    }

    menu_launch_log("spawn: " + app->get_name() + " → " + cmd);

    char **envp = nullptr;
    try
    {
        envp = build_launch_envp();
    } catch (const std::exception& e)
    {
        menu_launch_log(std::string("build_launch_envp failed: ") + e.what());
        return false;
    }

    GError *err = nullptr;
    gint argc = 0;
    gchar **argv = nullptr;
    if (!g_shell_parse_argv(cmd.c_str(), &argc, &argv, &err))
    {
        menu_launch_log(std::string("parse Exec failed: ") +
            (err ? err->message : "unknown"));
        g_clear_error(&err);
        g_strfreev(envp);
        return false;
    }

    /* Log a short env fingerprint so we can debug display/session issues. */
    {
        auto get = [&] (const char *k) -> std::string {
            for (char **e = envp; e && *e; ++e)
            {
                std::string s(*e);
                auto eq = s.find('=');
                if (eq != std::string::npos && s.substr(0, eq) == k)
                {
                    return s.substr(eq + 1);
                }
            }
            return {};
        };
        menu_launch_log("spawn env DISPLAY=" + get("DISPLAY") +
            " WAYLAND_DISPLAY=" + get("WAYLAND_DISPLAY") +
            " XDG_RUNTIME_DIR=" + get("XDG_RUNTIME_DIR") +
            " DBUS=" + (get("DBUS_SESSION_BUS_ADDRESS").empty() ? "unset" : "set") +
            " TERM=" + get("TERM"));
    }

    GPid child_pid = 0;
    gboolean ok = g_spawn_async(nullptr, argv, envp,
        (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_SEARCH_PATH_FROM_ENVP |
            G_SPAWN_CLOEXEC_PIPES | G_SPAWN_DO_NOT_REAP_CHILD),
        launch_child_setup, nullptr, &child_pid, &err);
    g_strfreev(argv);
    g_strfreev(envp);
    if (!ok)
    {
        menu_launch_log(std::string("spawn failed for ") + app->get_name() + ": " +
            (err ? err->message : "unknown"));
        g_clear_error(&err);
        return false;
    }
    menu_launch_log("spawned pid=" + std::to_string(static_cast<long>(child_pid)) +
        " for " + app->get_name());
    /* Reap asynchronously so we don't leave zombies; ignore exit status. */
    g_child_watch_add(child_pid, [] (GPid pid, gint, gpointer) {
        g_spawn_close_pid(pid);
    }, nullptr);
    return true;
}

/**
 * Launch desktop app with a clean session env (spawn primary).
 * Gio is secondary: it merges panel environ and can "succeed" while the child
 * dies or hands off to a dead single-instance owner.
 */
static bool launch_desktop_app(const Glib::RefPtr<Gio::DesktopAppInfo>& app)
{
    if (!app)
    {
        return false;
    }
    menu_launch_log(std::string("launch: ") + app->get_name() +
        " exec=" + app->get_executable());

    try
    {
        (void)wf_shell::session_env_for_app_launch(nullptr);
    } catch (...)
    {}

    pre_launch_app_cleanup(app);

    /* Primary: clean-env spawn (reliable on FreeBSD + agent-restarted panel). */
    if (spawn_desktop_app_fallback(app))
    {
        return true;
    }

    menu_launch_log("spawn path failed; trying Gio launch");
    auto display = Gdk::Display::get_default();
    Glib::RefPtr<Gio::AppLaunchContext> ctx;
    if (display)
    {
        ctx = display->get_app_launch_context();
    }
    if (!ctx)
    {
        ctx = Gio::AppLaunchContext::create();
    }
    inject_launch_env(ctx);

    bool ok = false;
    try
    {
        ok = app->launch(std::vector<Glib::RefPtr<Gio::File>>(), ctx);
    } catch (const std::exception& e)
    {
        menu_launch_log(std::string("Gio launch exception: ") + e.what());
        ok = false;
    }

    if (!ok)
    {
        menu_launch_log(std::string("FAILED to launch ") + app->get_name());
    }
    return ok;
}

WfMenuItem::WfMenuItem(WayfireMenu *_menu, Glib::RefPtr<Gio::DesktopAppInfo> app) :
    Gtk::FlowBoxChild(), menu(_menu), app_info(app)
{
    image.set((const Glib::RefPtr<const Gio::Icon>&)app->get_icon());
    image.add_css_class("default-icon");
    label.set_text(app->get_name());
    label.set_ellipsize(Pango::EllipsizeMode::END);

    extra_actions_button.add_css_class("flat");
    extra_actions_button.add_css_class("app-button-extras");
    extra_actions_button.set_direction(Gtk::ArrowType::RIGHT);
    extra_actions_button.set_has_frame(false);

    box.set_expand(false);
    box.add_css_class("app-button");
    button.add_css_class("flat");
    button.add_css_class("app-button");

    /*
     * Primary activation must always launch the app.
     *
     * Grid mode previously made apps with Desktop Actions a MenuButton as the
     * sole child — left click only opened the action popup and never ran Exec.
     * Gestures on an inner box inside Button/MenuButton were also eaten by GTK4
     * so many apps appeared to do nothing.
     *
     * Fix: always use Gtk::Button + signal_clicked for launch; keep actions on
     * a separate MenuButton (list) or right-click / long-press (grid).
     */
    signals.push_back(button.signal_clicked().connect([this] ()
    {
        on_click();
    }));

    auto right_click_g = Gtk::GestureClick::create();
    auto long_press_g  = Gtk::GestureLongPress::create();
    right_click_g->set_button(3);
    long_press_g->set_touch_only(true);

    auto open_extras = [this] ()
    {
        if (has_actions)
        {
            extra_actions_button.popup();
        }
    };

    signals.push_back(right_click_g->signal_pressed().connect(
        [=] (int, double, double)
    {
        right_click_g->set_state(Gtk::EventSequenceState::CLAIMED);
        open_extras();
    }));
    signals.push_back(long_press_g->signal_pressed().connect(
        [=] (double, double)
    {
        long_press_g->set_state(Gtk::EventSequenceState::CLAIMED);
        open_extras();
    }));

    if (menu->menu_list)
    {
        label.set_hexpand(true);
        label.set_halign(Gtk::Align::START);
        label.set_xalign(0.0);
        list_item.set_hexpand(true);
        box.set_hexpand(true);
        set_hexpand(true);
        box.set_orientation(Gtk::Orientation::HORIZONTAL);
        extra_actions_button.set_halign(Gtk::Align::END);
        extra_actions_button.set_icon_name("arrow-right");

        list_item.append(image);
        list_item.append(label);
        button.set_child(list_item);

        box.append(button);
        box.append(extra_actions_button);
        box.add_controller(right_click_g);
        box.add_controller(long_press_g);
        set_child(box);
    } else
    {
        /* Grid: always launch via button; never wrap the tile in MenuButton. */
        label.set_max_width_chars(0);
        box.set_orientation(Gtk::Orientation::VERTICAL);
        box.append(image);
        box.append(label);
        button.set_child(box);
        button.add_controller(right_click_g);
        button.add_controller(long_press_g);

        /* Outer list_item holds the launch button (+ optional actions chevron). */
        list_item.set_orientation(Gtk::Orientation::VERTICAL);
        list_item.append(button);
        list_item.append(extra_actions_button);
        set_child(list_item);
    }

    m_menu  = Gio::Menu::create();
    actions = Gio::SimpleActionGroup::create();
    extra_actions_button.hide();
    has_actions = false;

    for (auto action : app->list_actions())
    {
        has_actions = true;
        std::stringstream ss;
        ss << "app." << action;
        std::string full_action = ss.str();

        auto menu_item = Gio::MenuItem::create(app_info->get_action_name(action), full_action);

        auto action_obj = Gio::SimpleAction::create(action);
        signals.push_back(action_obj->signal_activate().connect(
            [this, action] (Glib::VariantBase)
        {
            menu_launch_log(std::string("launch_action: ") + app_info->get_name() +
                " action=" + action);
            auto ctx = Gdk::Display::get_default()->get_app_launch_context();
            inject_launch_env(ctx);
            try
            {
                app_info->launch_action(action, ctx);
            } catch (const std::exception& e)
            {
                menu_launch_log(std::string("launch_action failed: ") + e.what());
            }
            menu->hide_menu();
        }));
        m_menu->append_item(menu_item);
        actions->add_action(action_obj);

        if (menu->menu_list)
        {
            extra_actions_button.show();
        }
    }

    extra_actions_button.set_menu_model(m_menu);
    extra_actions_button.insert_action_group("app", actions);

    set_has_tooltip();
    signals.push_back(signal_query_tooltip().connect([=] (int, int, bool,
                                                          const std::shared_ptr<Gtk::Tooltip>& tooltip) ->
        bool
    {
        tooltip->set_text(app->get_name());
        return true;
    }, false));
}

WfMenuItem::~WfMenuItem()
{
    for (auto signal : signals)
    {
        signal.disconnect();
    }
}

void WfMenuItem::on_click()
{
    /* Button clicked + FlowBox child-activated can both fire for one click. */
    if (launch_in_progress)
    {
        return;
    }
    launch_in_progress = true;
    launch_desktop_app(app_info);
    menu->hide_menu();
    Glib::signal_timeout().connect([this] ()
    {
        launch_in_progress = false;
        return false; /* one-shot */
    }, 750);
}

void WfMenuItem::set_search_value(uint32_t value)
{
    search_value = value;
}

uint32_t WfMenuItem::get_search_value()
{
    return search_value;
}

/* Fuzzy search for pattern in text. We use a greedy algorithm as follows:
 * As long as the pattern isn't matched, try to match the leftmost unmatched
 * character in pattern with the first occurence of this character after the
 * partial match. In the end, we just check if we successfully matched all
 * characters */
static bool fuzzy_match(Glib::ustring text, Glib::ustring pattern)
{
    size_t i = 0, // next character in pattern to match
        j    = 0; // the first unmatched character in text

    while (i < pattern.length() && j < text.length())
    {
        /* Found a match, advance both pointers */
        if (pattern[i] == text[j])
        {
            ++i;
            ++j;
        } else
        {
            /* Try to match current unmatched character in pattern with the next
             * character in text */
            ++j;
        }
    }

    /* If this happens, then we have already matched all characters */
    return i == pattern.length();
}

uint32_t WfMenuItem::fuzzy_match(Glib::ustring pattern)
{
    uint32_t match_score = 0;
    Glib::ustring name   = app_info->get_name();
    Glib::ustring long_name = app_info->get_display_name();
    Glib::ustring progr     = app_info->get_executable();

    auto name_lower = name.lowercase();
    auto long_name_lower = long_name.lowercase();
    auto progr_lower     = progr.lowercase();
    auto pattern_lower   = pattern.lowercase();

    if (::fuzzy_match(progr_lower, pattern_lower))
    {
        match_score += 100;
    }

    if (::fuzzy_match(name_lower, pattern_lower))
    {
        match_score += 100;
    }

    if (::fuzzy_match(long_name_lower, pattern_lower))
    {
        match_score += 10;
    }

    return match_score;
}

uint32_t WfMenuItem::matches(Glib::ustring pattern)
{
    uint32_t match_score    = 0;
    Glib::ustring long_name = app_info->get_display_name();
    Glib::ustring name  = app_info->get_name();
    Glib::ustring progr = app_info->get_executable();
    Glib::ustring descr = app_info->get_description();

    auto name_lower = name.lowercase();
    auto long_name_lower = long_name.lowercase();
    auto progr_lower     = progr.lowercase();
    auto descr_lower     = descr.lowercase();
    auto pattern_lower   = pattern.lowercase();

    auto pos = name_lower.find(pattern_lower);
    if (pos != name_lower.npos)
    {
        match_score += 1000 - pos;
    }

    pos = progr_lower.find(pattern_lower);
    if (pos != progr_lower.npos)
    {
        match_score += 1000 - pos;
    }

    pos = long_name_lower.find(pattern_lower);
    if (pos != long_name_lower.npos)
    {
        match_score += 500 - pos;
    }

    pos = descr_lower.find(pattern_lower);
    if (pos != descr_lower.npos)
    {
        match_score += 300 - pos;
    }

    return match_score;
}

bool WfMenuItem::operator <(const WfMenuItem& other)
{
    return Glib::ustring(app_info->get_name()).lowercase() <
           Glib::ustring(other.app_info->get_name()).lowercase();
}

void WayfireMenu::load_menu_item(AppInfo app_info)
{
    if (!app_info)
    {
        return;
    }

    if (app_info->get_nodisplay())
    {
        return;
    }

    auto name = app_info->get_name();
    auto exec = app_info->get_executable();
    /* If we don't have the following, then the entry won't be useful anyway,
     * so we should skip it */
    if (name.empty() || !app_info->get_icon() || exec.empty())
    {
        return;
    }

    /* Already created such a launcher, skip */
    if (loaded_apps.count({name, exec}))
    {
        return;
    }

    loaded_apps.insert({name, exec});

    /* Check if this has a 'OnlyShownIn' for a different desktop env
    *  If so, we throw it in a pile at the bottom just to be safe */
    if (!app_info->should_show())
    {
        add_category_app("Hidden", app_info);
        return;
    }

    add_category_app("All", app_info);

    /* Split the Categories, iterate to place into submenus */
    std::stringstream categories_stream(app_info->get_categories());
    std::string segment;

    while (std::getline(categories_stream, segment, ';'))
    {
        add_category_app(segment, app_info);
    }
}

void WayfireMenu::add_category_app(std::string category, AppInfo app)
{
    /* Filter for allowed categories */
    if (category_list.count(category) == 1)
    {
        category_list[category]->items.push_back(app);
    }
}

void WayfireMenu::populate_menu_categories()
{
    // Ensure the category list is empty
    for (auto child : category_box.get_children())
    {
        category_box.remove(*child);
        delete child;
    }

    // Iterate allowed categories in order
    for (auto category_name : category_order)
    {
        if (category_list.count(category_name) == 1)
        {
            auto& category = category_list[category_name];
            if (category->items.size() > 0)
            {
                auto icon_name = category->get_icon_name();
                auto name = category->get_name();
                auto category_button = new WfMenuCategoryButton(this, category_name, name, icon_name);
                category_box.append(*category_button);
            }
        } else
        {
            std::cerr << "Category in orderlist without Category object : " << category << std::endl;
        }
    }
}

void WayfireMenu::populate_menu_items(std::string category)
{
    /* Ensure the flowbox is empty */
    for (auto child : flowbox.get_children())
    {
        flowbox.remove(*child);
        delete child;
    }

    for (auto app_info : category_list[category]->items)
    {
        auto app = new WfMenuItem(this, app_info);
        flowbox.append(*app);
    }
}

static bool ends_with(std::string text, std::string pattern)
{
    if (text.length() < pattern.length())
    {
        return false;
    }

    return text.substr(text.length() - pattern.length()) == pattern;
}

void WayfireMenu::load_menu_items_from_dir(std::string path)
{
    /* Expand path */
    auto dir = opendir(path.c_str());
    if (!dir)
    {
        return;
    }

    /* Iterate over all files in the directory */
    dirent *file;
    while ((file = readdir(dir)) != 0)
    {
        /* Skip hidden files and folders */
        if (file->d_name[0] == '.')
        {
            continue;
        }

        auto fullpath = path + "/" + file->d_name;

        if (ends_with(fullpath, ".desktop"))
        {
            load_menu_item(Gio::DesktopAppInfo::create_from_filename(fullpath));
        }
    }
}

void WayfireMenu::load_menu_items_all()
{
    std::string home_dir = getenv("HOME");
    auto app_list = Gio::AppInfo::get_all();
    for (auto app : app_list)
    {
        auto desktop_app = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(app);
        load_menu_item(desktop_app);
    }

    load_menu_items_from_dir(home_dir + "/Desktop");
}

void WayfireMenu::on_search_changed()
{
    if (menu_show_categories)
    {
        if (search_entry.get_text().length() == 0)
        {
            /* Text has been unset, show categories again */
            populate_menu_items(category);
            category_scrolled_window.show();
        } else
        {
            /* User is filtering, hide categories, ignore chosen category */
            populate_menu_items("All");
        }
    }

    m_sort_names  = search_entry.get_text().length() == 0;
    fuzzy_filter  = false;
    count_matches = 0;
    flowbox.unselect_all();
    flowbox.invalidate_filter();
    flowbox.invalidate_sort();

    /* We got no matches, try to fuzzy-match */
    if ((count_matches <= 0) && fuzzy_search_enabled)
    {
        fuzzy_filter = true;
        flowbox.unselect_all();
        flowbox.invalidate_filter();
        flowbox.invalidate_sort();
    }

    select_first_flowbox_item();
}

bool WayfireMenu::on_filter(Gtk::FlowBoxChild *child)
{
    auto button = dynamic_cast<WfMenuItem*>(child);
    assert(button);

    auto text = search_entry.get_text();
    uint32_t match_score = this->fuzzy_filter ?
        button->fuzzy_match(text) : button->matches(text);

    button->set_search_value(match_score);
    if (match_score > 0)
    {
        this->count_matches++;
        return true;
    }

    return false;
}

bool WayfireMenu::on_sort(Gtk::FlowBoxChild *a, Gtk::FlowBoxChild *b)
{
    auto b1 = dynamic_cast<WfMenuItem*>(a);
    auto b2 = dynamic_cast<WfMenuItem*>(b);
    assert(b1 && b2);

    if (m_sort_names)
    {
        return *b2 < *b1;
    }

    return b2->get_search_value() > b1->get_search_value();
}

void WayfireMenu::on_popover_shown()
{
    search_entry.delete_text(0, search_entry.get_text().length());
    on_search_changed();
    set_category("All");
    flowbox.unselect_all();
    search_entry.grab_focus();
}

bool WayfireMenu::update_icon()
{
    /* Theme-driven only — pack art, then Wayfire file, then icon names. */
    int px = 32;
    try
    {
        WfOption<int> sz{"panel/menu_icon_size"};
        if (sz.value() > 0)
        {
            px = sz.value();
        }
    } catch (...)
    {}

    bool ok = false;
    for (const auto& cand : menu_icon_candidates_for_theme())
    {
        if (set_menu_button_icon(main_image, cand, px))
        {
            ok = true;
            break;
        }
    }
    if (!ok)
    {
        /* Never blank: force Wayfire name even if has_icon() lied */
        try
        {
            main_image.set_from_icon_name("wayfire");
            ok = true;
        } catch (...)
        {
            main_image.set_from_icon_name("application-x-executable");
        }
    }

    main_image.add_css_class("menu-icon");
    main_image.add_css_class("widget-icon");
    main_image.add_css_class("default-icon");
    main_image.set_visible(true);
    return true;
}

void WayfireMenu::setup_popover_layout()
{
    button->set_popup_child(popover_layout_box);

    popover_layout_box.append(app_scrolled_window);
    popover_layout_box.append(category_scrolled_window);
    popover_layout_box.append(search_entry);
    popover_layout_box.append(separator);
    popover_layout_box.append(box_bottom);

    flowbox.set_selection_mode(Gtk::SelectionMode::SINGLE);
    flowbox.set_activate_on_single_click(true);
    flowbox.set_valign(Gtk::Align::START);
    flowbox.set_homogeneous(true);
    flowbox.set_sort_func(sigc::mem_fun(*this, &WayfireMenu::on_sort));
    flowbox.set_filter_func(sigc::mem_fun(*this, &WayfireMenu::on_filter));
    flowbox.add_css_class("app-list");
    flowbox.set_vexpand(true);
    flowbox.set_hexpand(true);

    /* Belt-and-suspenders: keyboard / FlowBox activation also launches. */
    signals.push_back(flowbox.signal_child_activated().connect(
        [] (Gtk::FlowBoxChild *child)
    {
        if (auto *item = dynamic_cast<WfMenuItem*>(child))
        {
            item->on_click();
        }
    }));

    flowbox_container.append(flowbox);
    app_scrolled_window.set_child(flowbox_container);
    app_scrolled_window.add_css_class("app-list-scroll");
    app_scrolled_window.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    category_box.add_css_class("category-list");
    category_box.set_orientation(Gtk::Orientation::VERTICAL);

    category_scrolled_window.set_child(category_box);
    category_scrolled_window.add_css_class("categtory-list-scroll");
    category_scrolled_window.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    category_scrolled_window.set_vexpand(true);

    search_entry.add_css_class("app-search");

    signals.push_back((search_entry.signal_changed().connect(
        [this] ()
    {
        on_search_changed();
    })));
    auto typing_gesture = Gtk::EventControllerKey::create();
    typing_gesture->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    signals.push_back(typing_gesture->signal_key_pressed().connect([=] (guint keyval, guint keycode,
                                                                        Gdk::ModifierType state)
    {
        Gtk::Widget *focused = nullptr;
        auto root = popover_layout_box.get_root();
        if (root)
        {
            focused = root->get_focus();
        }

        if ((keyval == GDK_KEY_Return) || (keyval == GDK_KEY_KP_Enter))
        {
            auto children = flowbox.get_selected_children();
            if (children.size() == 1)
            {
                auto child = dynamic_cast<WfMenuItem*>(children[0]);
                child->on_click();
            }

            return true;
        } else if (keyval == GDK_KEY_Escape)
        {
            button->popdown();
            return true;
        } else if ((keyval == GDK_KEY_Up) ||
                   (keyval == GDK_KEY_Down) ||
                   (keyval == GDK_KEY_Left) ||
                   (keyval == GDK_KEY_Right))
        {
            return false;
        } else if (focused && focused->is_ancestor(search_entry))
        {
            return false;
        } else
        {
            if (!search_entry.grab_focus())
            {
                std::cerr << "Unable to steal focus to entry" << std::endl;
            }

            // this is a hack to still try to get the key propersly to resume
            // search, as we can’t focus the entry and have the key be used by
            // it. Better than nothing, but it fails to account for keys other
            // than simple letters, and still focuses on other keys.
            std::string input = gdk_keyval_name(keyval);
            if (input.length() == 1)
            {
                search_entry.set_text(search_entry.get_text() + input);
            }

            auto pos = search_entry.get_text().length();
            search_entry.select_region(pos, pos);
            on_search_changed();
            return false;
        }
    }, false));
    popover_layout_box.add_controller(typing_gesture);

    layout = std::make_shared<WfMenuLayout>(this);
    popover_layout_box.set_layout_manager(layout);
}

void WayfireMenu::update_popover_layout()
{
    if (!menu_show_categories)
    {
        category_scrolled_window.hide();
    } else
    {
        category_scrolled_window.show();
    }

    if (menu_list)
    {
        flowbox.set_max_children_per_line(1);
    } else
    {
        flowbox.set_max_children_per_line(-1);
    }
}

void WayfireLogoutUI::on_logout_click()
{
    ui.hide();
    g_spawn_command_line_async(logout_command.value().c_str(), NULL);
}

void WayfireLogoutUI::on_reboot_click()
{
    ui.hide();
    const char *cmd = reboot_cmd.empty() ? reboot_command.value().c_str()
                                          : reboot_cmd.c_str();
    g_spawn_command_line_async(cmd, NULL);
}

void WayfireLogoutUI::on_shutdown_click()
{
    ui.hide();
    const char *cmd = shutdown_cmd.empty() ? shutdown_command.value().c_str()
                                            : shutdown_cmd.c_str();
    g_spawn_command_line_async(cmd, NULL);
}

void WayfireLogoutUI::on_suspend_click()
{
    ui.hide();
    const char *cmd = suspend_cmd.empty() ? suspend_command.value().c_str()
                                           : suspend_cmd.c_str();
    g_spawn_command_line_async(cmd, NULL);
}

void WayfireLogoutUI::on_hibernate_click()
{
    ui.hide();
    const char *cmd = hibernate_cmd.empty() ? hibernate_command.value().c_str()
                                             : hibernate_cmd.c_str();
    g_spawn_command_line_async(cmd, NULL);
}

void WayfireLogoutUI::on_switchuser_click()
{
    ui.hide();
    const char *cmd = switchuser_cmd.empty() ? switchuser_command.value().c_str()
                                               : switchuser_cmd.c_str();
    g_spawn_command_line_async(cmd, NULL);
}

void WayfireLogoutUI::on_cancel_click()
{
    ui.hide();
}

#define LOGOUT_BUTTON_SIZE  125
#define LOGOUT_BUTTON_MARGIN 10

void WayfireLogoutUI::create_logout_ui_button(WayfireLogoutUIButton *button, const char *icon,
    const char *label)
{
    button->button.set_size_request(LOGOUT_BUTTON_SIZE, LOGOUT_BUTTON_SIZE);
    button->image.set_from_icon_name(icon);
    button->label.set_text(label);
    button->layout.set_orientation(Gtk::Orientation::VERTICAL);
    button->layout.set_halign(Gtk::Align::CENTER);
    button->layout.append(button->image);
    button->image.set_icon_size(Gtk::IconSize::LARGE);
    button->image.set_vexpand(true);
    button->layout.append(button->label);
    button->button.set_child(button->layout);
}

WayfireLogoutUI::WayfireLogoutUI()
{
    /* Query platform capabilities once.  Buttons are hidden when not available
     * or not permitted for the current user. */
    auto controller = WFPowerController::create();
    auto reboot_cap     = controller->query(WFPowerController::Action::Reboot);
    auto shutdown_cap   = controller->query(WFPowerController::Action::Shutdown);
    auto suspend_cap    = controller->query(WFPowerController::Action::Suspend);
    auto hibernate_cap  = controller->query(WFPowerController::Action::Hibernate);
    auto switchuser_cap = controller->query(WFPowerController::Action::SwitchUser);

    /* Populate platform-specific command strings.  If the platform provides a
     * command, it overrides the config-file default. */
    if (reboot_cap.available)     reboot_cmd     = reboot_cap.command;
    if (shutdown_cap.available)   shutdown_cmd   = shutdown_cap.command;
    if (suspend_cap.available)   suspend_cmd    = suspend_cap.command;
    if (hibernate_cap.available)  hibernate_cmd  = hibernate_cap.command;
    if (switchuser_cap.available) switchuser_cmd = switchuser_cap.command;

    create_logout_ui_button(&suspend, "emblem-synchronizing", "Suspend");
    suspend.button.set_visible(suspend_cap.available && suspend_cap.permitted);
    signals.push_back(suspend.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_suspend_click)));
    main_layout.attach(suspend.button, 0, 0, 1, 1);

    create_logout_ui_button(&hibernate, "weather-clear-night", "Hibernate");
    hibernate.button.set_visible(hibernate_cap.available && hibernate_cap.permitted);
    signals.push_back(hibernate.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_hibernate_click)));
    main_layout.attach(hibernate.button, 1, 0, 1, 1);

    create_logout_ui_button(&switchuser, "system-users", "Switch User");
    switchuser.button.set_visible(switchuser_cap.available && switchuser_cap.permitted);
    signals.push_back(switchuser.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_switchuser_click)));
    main_layout.attach(switchuser.button, 2, 0, 1, 1);

    create_logout_ui_button(&logout, "system-log-out", "Log Out");
    /* Logout is always available (wayland-logout works on all platforms). */
    signals.push_back(logout.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_logout_click)));
    main_layout.attach(logout.button, 0, 1, 1, 1);

    create_logout_ui_button(&reboot, "system-reboot", "Reboot");
    reboot.button.set_visible(reboot_cap.available && reboot_cap.permitted);
    signals.push_back(reboot.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_reboot_click)));
    main_layout.attach(reboot.button, 1, 1, 1, 1);

    create_logout_ui_button(&shutdown, "system-shutdown", "Shut Down");
    shutdown.button.set_visible(shutdown_cap.available && shutdown_cap.permitted);
    signals.push_back(shutdown.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_shutdown_click)));
    main_layout.attach(shutdown.button, 2, 1, 1, 1);

    cancel.button.set_size_request(100, 50);
    cancel.button.set_label("Cancel");
    main_layout.attach(cancel.button, 1, 2, 1, 1);
    signals.push_back(cancel.button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireLogoutUI::on_cancel_click)));

    main_layout.set_row_spacing(LOGOUT_BUTTON_MARGIN);
    main_layout.set_column_spacing(LOGOUT_BUTTON_MARGIN);
    /* Make surfaces layer shell */
    gtk_layer_init_for_window(ui.gobj());
    gtk_layer_set_layer(ui.gobj(), GTK_LAYER_SHELL_LAYER_OVERLAY);

    gtk_layer_set_anchor(ui.gobj(), GTK_LAYER_SHELL_EDGE_TOP, true);
    gtk_layer_set_anchor(ui.gobj(), GTK_LAYER_SHELL_EDGE_BOTTOM, true);
    gtk_layer_set_anchor(ui.gobj(), GTK_LAYER_SHELL_EDGE_LEFT, true);
    gtk_layer_set_anchor(ui.gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, true);
    main_layout.set_valign(Gtk::Align::CENTER);
    box.set_center_widget(main_layout);
    box.set_hexpand(true);
    box.set_vexpand(true);
    ui.set_child(box);
    ui.add_css_class("logout");
    auto display = ui.get_display();
    auto css_provider = Gtk::CssProvider::create();
    css_provider->load_from_data("window.logout { background-color: rgba(0, 0, 0, 0.5); }");
    Gtk::StyleContext::add_provider_for_display(display,
        css_provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
}

WayfireLogoutUI::~WayfireLogoutUI()
{
    for (auto signal : signals)
    {
        signal.disconnect();
    }
}

void WayfireMenu::on_logout_click()
{
    button->popdown();
    if (!menu_logout_command.value().empty())
    {
        g_spawn_command_line_async(menu_logout_command.value().c_str(), NULL);
        return;
    }

    /* If no command specified for logout, show our own logout window */
    logout_ui->ui.present();
}

void WayfireMenu::refresh()
{
    loaded_apps.clear();
    for (auto& [key, category] : category_list)
    {
        category->items.clear();
    }

    for (auto child : flowbox.get_children())
    {
        flowbox.remove(*child);
        delete child;
    }

    load_menu_items_all();
    populate_menu_categories();
    populate_menu_items("All");
}

static void app_info_changed(GAppInfoMonitor *gappinfomonitor, gpointer user_data)
{
    WayfireMenu *menu = (WayfireMenu*)user_data;

    menu->refresh();
}

void WayfireMenu::init(Gtk::Box *container)
{
    /* https://specifications.freedesktop.org/menu-spec/latest/apa.html#main-category-registry
     * Using the 'Main' categories, with names and icons assigned
     * Any Categories in .desktop files that are not in this list are ignored */
    category_list["All"]     = std::make_unique<WfMenuCategory>("All", "applications-other");
    category_list["Network"] = std::make_unique<WfMenuCategory>("Internet",
        "applications-internet");
    category_list["Education"] = std::make_unique<WfMenuCategory>("Education",
        "applications-education");
    category_list["Office"] = std::make_unique<WfMenuCategory>("Office",
        "applications-office");
    category_list["Development"] = std::make_unique<WfMenuCategory>("Development",
        "applications-development");
    category_list["Graphics"] = std::make_unique<WfMenuCategory>("Graphics",
        "applications-graphics");
    category_list["AudioVideo"] = std::make_unique<WfMenuCategory>("Multimedia",
        "applications-multimedia");
    category_list["Game"] = std::make_unique<WfMenuCategory>("Games",
        "applications-games");
    category_list["Science"] = std::make_unique<WfMenuCategory>("Science",
        "applications-science");
    category_list["Settings"] = std::make_unique<WfMenuCategory>("Settings",
        "preferences-desktop");
    category_list["System"] = std::make_unique<WfMenuCategory>("System",
        "applications-system");
    category_list["Utility"] = std::make_unique<WfMenuCategory>("Accessories",
        "applications-utilities");
    category_list["Hidden"] = std::make_unique<WfMenuCategory>("Other Desktops",
        "user-desktop");

    main_image.add_css_class("default-icon");
    main_image.add_css_class("menu-icon");

    signals.push_back(output->toggle_menu_signal().connect(sigc::mem_fun(*this, &WayfireMenu::toggle_menu)));

    // configuration reloading callbacks
    menu_icon.set_callback([=] () { update_icon(); });
    flowbox_spacing.set_callback([=] ()
    {
        flowbox.set_column_spacing(flowbox_spacing.value());
        flowbox.set_column_spacing(flowbox_spacing.value());
    });
    menu_min_category_width.set_callback([=] () { update_size(); });
    menu_min_content_height.set_callback([=] () { update_size(); });
    menu_min_content_width.set_callback([=] () { update_size(); });
    panel_position.set_callback([=] () { update_popover_layout(); });
    menu_show_categories.set_callback([=] () { update_popover_layout(); });
    menu_list.set_callback([=] () { update_popover_layout(); });

    button = std::make_unique<WayfireMenuWidget>("panel", "menu");
    button->append(main_image);
    button->set_keyboard_interactive(true);
    button->add_css_class("menu-button");
    button->open_on(1); /* Open menu on left click */
    signals.push_back(button->signal_popup().connect(
        sigc::mem_fun(*this, &WayfireMenu::on_popover_shown)));

    if (!update_icon())
    {
        return;
    }

    signals.push_back(button->property_scale_factor().signal_changed().connect(
        [=] () {update_icon(); }));

    button->set_popup_child(popover_layout_box);

    container->append(box);
    box.append(*button);

    auto click_gesture = Gtk::GestureClick::create();
    click_gesture->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    signals.push_back(click_gesture->signal_pressed().connect([=] (int count, double x, double y)
    {
        click_gesture->set_state(Gtk::EventSequenceState::CLAIMED);
    }));
    signals.push_back(click_gesture->signal_released().connect([=] (int count, double x, double y)
    {
        toggle_menu();
    }));
    box.add_controller(click_gesture);

    auto menu_fs_changed = [=]
    {
        button->set_fullscreen(menu_fullscreen.value());
    };
    menu_fullscreen.set_callback(menu_fs_changed);
    menu_fs_changed();

    box_bottom.set_orientation(Gtk::Orientation::HORIZONTAL);
    box_bottom.set_hexpand(true);
    box_bottom.set_halign(Gtk::Align::FILL);

    /* Settings button (left) — themes/display/etc. live in wf-settings. */
    auto *settings_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    settings_box->set_margin_start(15);
    settings_box->set_halign(Gtk::Align::START);

    settings_button.set_label("Settings");
    settings_button.add_css_class("flat");
    settings_button.set_tooltip_text("Desktop and panel settings");
    try
    {
        settings_image.set_from_icon_name("preferences-system");
        settings_button.set_child(settings_image);
        /* Keep accessible name */
        settings_button.set_tooltip_text("Settings");
    } catch (...)
    {}
    signals.push_back(settings_button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireMenu::on_settings_clicked)));
    settings_box->append(settings_button);
    box_bottom.append(*settings_box);

    /* Spacer keeps logout pinned to the trailing edge (always in view). */
    auto *spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    spacer->set_halign(Gtk::Align::FILL);
    box_bottom.append(*spacer);

    logout_image.set_icon_size(Gtk::IconSize::LARGE);
    logout_image.set_from_icon_name("system-shutdown");
    logout_button.add_css_class("flat");
    logout_button.set_tooltip_text("Log out…");
    signals.push_back(logout_button.signal_clicked().connect(
        sigc::mem_fun(*this, &WayfireMenu::on_logout_click)));
    logout_button.set_margin_end(10);
    logout_button.set_margin_start(6);
    logout_button.set_halign(Gtk::Align::END);
    logout_button.set_hexpand(false);
    logout_button.set_child(logout_image);
    box_bottom.append(logout_button);

    popover_layout_box.set_orientation(Gtk::Orientation::VERTICAL);

    logout_ui = std::make_unique<WayfireLogoutUI>();

    load_menu_items_all();
    setup_popover_layout();
    update_popover_layout();
    populate_menu_categories();
    populate_menu_items("All");
    app_info_monitor = g_app_info_monitor_get();
    app_info_monitor_changed_handler_id =
        g_signal_connect(app_info_monitor, "changed", G_CALLBACK(app_info_changed), this);

    box.show();
    main_image.show();
    button->show();

    signals.push_back(button->get_scroll().signal_map().connect([=] ()
    {
        update_size();
    }));
}

void WayfireMenu::update_size()
{
    int width  = button->get_scroll().get_width();
    int height = button->get_scroll().get_height();
    if ((width <= 0) || (height <= 0))
    {
        /* Not yet allocated, do it next tick */
        width  = menu_min_content_width;
        height = menu_min_content_height;
        if (menu_show_categories.value())
        {
            width += menu_min_category_width;
        }

        layout->set_limit(width, height);

        Glib::signal_idle().connect_once([=] ()
        {
            update_size();
        });
        return;
    }

    /* We know the size of the outside of the scrollbox, use it */
    layout->set_limit(width, height);
}

void WayfireMenu::toggle_menu()
{
    search_entry.set_text("");

    button->toggle();
}

void WayfireMenu::hide_menu()
{
    button->popdown();
}

void WayfireMenu::set_category(std::string in_category)
{
    category = in_category;
    populate_menu_items(in_category);
}

void WayfireMenu::select_first_flowbox_item()
{
    auto child = flowbox.get_child_at_index(0);
    if (child)
    {
        auto cast_child = dynamic_cast<WfMenuItem*>(child);
        if (cast_child)
        {
            flowbox.select_child(*cast_child);
        }
    }
}

WayfireMenu::~WayfireMenu()
{
    if (app_info_monitor)
    {
        g_signal_handler_disconnect(app_info_monitor, app_info_monitor_changed_handler_id);
    }

    for (auto signal : signals)
    {
        signal.disconnect();
    }
}



void WayfireMenu::on_settings_clicked()
{
    /* Theme/display/etc. live in wf-settings (GTK4). Launch with session env. */
    hide_menu();
    try
    {
        (void)wf_shell::session_env_for_app_launch(nullptr);
    } catch (...)
    {}
    GError *err = nullptr;
    /* Prefer installed binary; fall back to PATH. */
    const char *candidates[] = {
        "wf-settings",
        "/home/mlapointe/.local/bin/wf-settings",
        nullptr
    };
    bool ok = false;
    for (int i = 0; candidates[i]; ++i)
    {
        g_clear_error(&err);
        if (g_spawn_command_line_async(candidates[i], &err))
        {
            ok = true;
            break;
        }
    }
    if (!ok)
    {
        std::cerr << "wf-panel: failed to launch wf-settings: "
                  << (err ? err->message : "not found") << std::endl;
        g_clear_error(&err);
    }
}
