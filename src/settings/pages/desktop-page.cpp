#include "desktop-page.hpp"

#include "config-backend.hpp"
#include "ini-file.hpp"
#include "shell-json-config.hpp"
#include "ux-labels.hpp"

#include <wayfire/nonstd/json.hpp>

#include <cstdlib>
#include <map>
#include <string>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <thread>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace wf_settings
{
namespace
{
std::string url_encode(const std::string& value)
{
    std::ostringstream escaped;
    escaped << std::hex;
    for (char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped << c;
        }
        else
        {
            escaped << '%' << std::setw(2) << std::setfill('0') << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

std::string escape_json_str(const std::string& s)
{
    std::string res;
    for (char c : s)
    {
        if (c == '"') res += "\\\"";
        else if (c == '\\') res += "\\\\";
        else if (c == '\n') res += "\\n";
        else if (c == '\r') res += "\\r";
        else if (c == '\t') res += "\\t";
        else res += c;
    }
    return res;
}

void enforce_cache_limit(const std::string& cache_dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(cache_dir, ec)) return;

    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    uintmax_t total_size = 0;
    for (const auto& entry : fs::directory_iterator(cache_dir, ec))
    {
        if (entry.is_regular_file(ec))
        {
            auto sz = entry.file_size(ec);
            if (!ec)
            {
                total_size += sz;
                auto t = entry.last_write_time(ec);
                if (!ec)
                {
                    files.push_back({t, entry.path()});
                }
            }
        }
    }

    const uintmax_t limit = 2ULL * 1024 * 1024 * 1024; // 2 GB limit
    if (total_size > limit)
    {
        std::sort(files.begin(), files.end(), [] (const auto& a, const auto& b) {
            return a.first < b.first;
        });

        for (const auto& f : files)
        {
            if (f.second.filename() == "metadata.json") continue;
            auto sz = fs::file_size(f.second, ec);
            if (!ec)
            {
                fs::remove(f.second, ec);
                if (!ec)
                {
                    total_size -= sz;
                    if (total_size <= limit) break;
                }
            }
        }
    }
}
} // namespace

DesktopPage::DesktopPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 12)
{
    set_margin(16);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Desktop &amp; wallpaper</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    auto help = Gtk::make_managed<Gtk::Label>(
        "Choose how many desktops you have and which picture covers the background.");
    help->set_wrap(true);
    help->add_css_class("dim-label");
    help->set_halign(Gtk::Align::START);
    append(*help);

    /* Workspaces */
    auto ws_frame = Gtk::make_managed<Gtk::Frame>();
    auto ws_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    ws_box->set_margin(12);
    auto ws_t = Gtk::make_managed<Gtk::Label>();
    ws_t->set_markup("<b>Workspaces</b>");
    ws_t->set_halign(Gtk::Align::START);
    ws_box->append(*ws_t);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_column_spacing(12);
    grid->set_row_spacing(8);

    auto lcols = Gtk::make_managed<Gtk::Label>("Across");
    lcols->set_halign(Gtk::Align::START);
    grid->attach(*lcols, 0, 0, 1, 1);
    vwidth = Gtk::make_managed<Gtk::SpinButton>();
    vwidth->set_range(1, 10);
    vwidth->set_increments(1, 1);
    vwidth->set_tooltip_text("How many desktops side by side");
    grid->attach(*vwidth, 1, 0, 1, 1);

    auto lrows = Gtk::make_managed<Gtk::Label>("Down");
    lrows->set_halign(Gtk::Align::START);
    grid->attach(*lrows, 0, 1, 1, 1);
    vheight = Gtk::make_managed<Gtk::SpinButton>();
    vheight->set_range(1, 10);
    vheight->set_increments(1, 1);
    vheight->set_tooltip_text("How many desktops stacked");
    grid->attach(*vheight, 1, 1, 1, 1);
    ws_box->append(*grid);

    ws_summary = Gtk::make_managed<Gtk::Label>();
    ws_summary->set_halign(Gtk::Align::START);
    ws_summary->add_css_class("dim-label");
    ws_box->append(*ws_summary);
    ws_frame->set_child(*ws_box);
    append(*ws_frame);

    auto update_ws = [this] () {
        int c = static_cast<int>(vwidth->get_value());
        int r = static_cast<int>(vheight->get_value());
        ws_summary->set_text("You will have " + std::to_string(c * r) +
            " workspace" + (c * r == 1 ? "" : "s") +
            " (" + std::to_string(c) + " × " + std::to_string(r) + "). "
            "Changing this usually needs a session restart.");
    };
    vwidth->signal_value_changed().connect(update_ws);
    vheight->signal_value_changed().connect(update_ws);

    /* Wallpaper */
    auto bg_frame = Gtk::make_managed<Gtk::Frame>();
    auto bg_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    bg_box->set_margin(12);
    auto bg_t = Gtk::make_managed<Gtk::Label>();
    bg_t->set_markup("<b>Wallpaper</b>");
    bg_t->set_halign(Gtk::Align::START);
    bg_box->append(*bg_t);

    /* Search bar for both local and online wallpapers */
    auto search_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    search_box->set_margin_bottom(8);
    wallpaper_search = Gtk::make_managed<Gtk::SearchEntry>();
    wallpaper_search->set_hexpand(true);
    wallpaper_search->set_placeholder_text("Search local and online wallpapers (e.g. by author)...");
    search_box->append(*wallpaper_search);
    bg_box->append(*search_box);

    auto local_lbl = Gtk::make_managed<Gtk::Label>();
    local_lbl->set_markup("<span size='small' weight='bold'>Local Wallpapers</span>");
    local_lbl->set_halign(Gtk::Align::START);
    bg_box->append(*local_lbl);

    /* Wallpaper visual flowbox */
    auto flow_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    flow_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    flow_scroll->set_min_content_height(170);
    flow_scroll->add_css_class("wallpaper-scroll-container");
    wallpaper_flow = Gtk::make_managed<Gtk::FlowBox>();
    wallpaper_flow->set_valign(Gtk::Align::START);
    wallpaper_flow->set_column_spacing(10);
    wallpaper_flow->set_row_spacing(10);
    wallpaper_flow->set_selection_mode(Gtk::SelectionMode::NONE);
    flow_scroll->set_child(*wallpaper_flow);
    bg_box->append(*flow_scroll);

    /* Discover Online Wallpapers section */
    online_section = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    online_section->set_margin_top(12);
    
    auto online_lbl = Gtk::make_managed<Gtk::Label>();
    online_lbl->set_markup("<span size='small' weight='bold'>Discover Online Wallpapers</span>");
    online_lbl->set_halign(Gtk::Align::START);
    online_section->append(*online_lbl);
    
    auto online_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    online_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    online_scroll->set_min_content_height(170);
    online_scroll->add_css_class("wallpaper-scroll-container");
    
    online_flow = Gtk::make_managed<Gtk::FlowBox>();
    online_flow->set_valign(Gtk::Align::START);
    online_flow->set_column_spacing(10);
    online_flow->set_row_spacing(10);
    online_flow->set_selection_mode(Gtk::SelectionMode::NONE);
    online_scroll->set_child(*online_flow);
    online_section->append(*online_scroll);
    
    online_status_lbl = Gtk::make_managed<Gtk::Label>("Loading feed...");
    online_status_lbl->set_halign(Gtk::Align::START);
    online_status_lbl->add_css_class("dim-label");
    online_section->append(*online_status_lbl);
    
    bg_box->append(*online_section);

    auto bg_grid = Gtk::make_managed<Gtk::Grid>();
    bg_grid->set_column_spacing(12);
    bg_grid->set_row_spacing(8);
    int r = 0;

    auto limg = Gtk::make_managed<Gtk::Label>("Custom path");
    limg->set_halign(Gtk::Align::START);
    bg_grid->attach(*limg, 0, r, 1, 1);
    auto bg_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    bg_image = Gtk::make_managed<Gtk::Entry>();
    bg_image->set_hexpand(true);
    bg_image->set_placeholder_text("Custom path to photo…");
    browse_btn = Gtk::make_managed<Gtk::Button>("Browse…");
    bg_row->append(*bg_image);
    bg_row->append(*browse_btn);
    bg_grid->attach(*bg_row, 1, r++, 1, 1);

    auto lfill = Gtk::make_managed<Gtk::Label>("Fit");
    lfill->set_halign(Gtk::Align::START);
    bg_grid->attach(*lfill, 0, r, 1, 1);
    bg_fill = Gtk::make_managed<Gtk::DropDown>();
    {
        std::vector<Glib::ustring> labels;
        for (const auto& p : ux::fill_mode_choices())
        {
            fill_values.push_back(p.first);
            labels.push_back(p.second);
        }
        bg_fill->set_model(Gtk::StringList::create(labels));
    }
    bg_fill->set_hexpand(true);
    bg_grid->attach(*bg_fill, 1, r++, 1, 1);

    fill_hint = Gtk::make_managed<Gtk::Label>();
    fill_hint->set_wrap(true);
    fill_hint->set_halign(Gtk::Align::START);
    fill_hint->add_css_class("dim-label");
    bg_grid->attach(*fill_hint, 1, r++, 1, 1);

    bg_random = Gtk::make_managed<Gtk::CheckButton>("Pick a random picture from the folder");
    bg_grid->attach(*bg_random, 0, r++, 2, 1);

    bg_box->append(*bg_grid);
    bg_frame->set_child(*bg_box);
    append(*bg_frame);

    auto live = [this] () {
        if (!filling)
        {
            save(nullptr);
        }
    };
    bg_fill->property_selected().signal_changed().connect([this, live] () {
        update_preview_hint();
        live();
    });
    browse_btn->signal_clicked().connect([this] () { on_browse(); });
    vwidth->signal_value_changed().connect([update_ws, live] () {
        update_ws();
        live();
    });
    vheight->signal_value_changed().connect([update_ws, live] () {
        update_ws();
        live();
    });
    bg_random->signal_toggled().connect(live);
    bg_image->signal_changed().connect([this, live] () {
        live();
        if (!filling)
        {
            refresh_wallpaper_previews();
        }
    });

    wallpaper_search->signal_search_changed().connect([this] () {
        std::string query = wallpaper_search->get_text();
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
        
        for (size_t i = 0; i < discovered_wallpapers.size(); ++i)
        {
            std::string path = discovered_wallpapers[i];
            std::string lower_path = path;
            std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
            
            bool match = lower_path.find(lower_query) != std::string::npos;
            if (i < wallpaper_buttons.size() && wallpaper_buttons[i])
            {
                wallpaper_buttons[i]->set_visible(match);
            }
        }
        
        update_online_grid(query);
    });

    refresh();
    update_ws();
}

void DesktopPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

void DesktopPage::update_preview_hint()
{
    auto i = bg_fill->get_selected();
    if (i >= fill_values.size())
    {
        fill_hint->set_text("");
        return;
    }
    const auto& v = fill_values[i];
    if (v == "fill_and_crop")
    {
        fill_hint->set_text("Fills the whole screen; may trim the edges of the photo.");
    } else if (v == "stretch")
    {
        fill_hint->set_text("Stretches the photo to the screen. Can look warped.");
    } else if (v == "preserve_aspect")
    {
        fill_hint->set_text("Shows the whole photo; you may see bars on the sides or top.");
    } else if (v == "centered")
    {
        fill_hint->set_text("Places the photo in the middle at its real size.");
    } else
    {
        fill_hint->set_text("");
    }
}

std::string DesktopPage::wayfire_ini() const
{
    if (const char *o = std::getenv("WAYFIRE_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wayfire.ini" : std::string{};
}

std::string DesktopPage::shell_ini() const
{
    if (const char *o = std::getenv("WF_SHELL_CONFIG_FILE"); o && o[0])
    {
        return o;
    }
    const char *h = std::getenv("HOME");
    return h ? std::string(h) + "/.config/wf-shell.ini" : std::string{};
}

void DesktopPage::refresh()
{
    filling = true;
    auto wf = wayfire_ini();
    auto sh = shell_ini();
    vwidth->set_value(wf_shell::ini_get_int(wf, "core", "vwidth", 3));
    vheight->set_value(wf_shell::ini_get_int(wf, "core", "vheight", 3));
    bg_image->set_text(wf_shell::ini_get(sh, "background", "image"));
    bg_random->set_active(wf_shell::ini_get_bool(sh, "background", "randomize", false));
    std::string fill = wf_shell::ini_get(sh, "background", "fill_mode");
    if (fill.empty())
    {
        fill = "fill_and_crop";
    }
    guint fi = 0;
    for (guint i = 0; i < fill_values.size(); ++i)
    {
        if (fill == fill_values[i])
        {
            fi = i;
            break;
        }
    }
    bg_fill->set_selected(fi);
    update_preview_hint();
    refresh_wallpaper_previews();
    if (online_images.empty())
    {
        fetch_online_feed();
    }
    filling = false;
    if (status)
    {
        status->set_text("");
    }
}

void DesktopPage::on_browse()
{
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Choose a wallpaper");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto images = Gtk::FileFilter::create();
    images->set_name("Pictures");
    images->add_mime_type("image/*");
    filters->append(images);
    dialog->set_filters(filters);
    dialog->open(*dynamic_cast<Gtk::Window*>(get_root()),
        [this, dialog] (Glib::RefPtr<Gio::AsyncResult>& result) {
            try
            {
                auto file = dialog->open_finish(result);
                if (file)
                {
                    bg_image->set_text(file->get_path());
                    save(nullptr);
                }
            } catch (...)
            {}
        });
}

bool DesktopPage::save(std::string *error)
{
    auto wf = wayfire_ini();
    /* Workspace grid is often not hot-reloadable — write carefully via dirty API. */
    const int nw = static_cast<int>(vwidth->get_value());
    const int nh = static_cast<int>(vheight->get_value());
    const int ow = wf_shell::ini_get_int(wf, "core", "vwidth", 3);
    const int oh = wf_shell::ini_get_int(wf, "core", "vheight", 3);
    bool workspace_changed = (nw != ow) || (nh != oh);
    if (workspace_changed)
    {
        std::string err;
        auto& b = ConfigBackend::instance();
        if (!b.set_wayfire_option("core", "vwidth", std::to_string(nw), &err) ||
            !b.set_wayfire_option("core", "vheight", std::to_string(nh), &err))
        {
            if (status)
            {
                status->set_text(err.empty() ? "We couldn't prepare your workspace layout. Please try again." : err);
            }
            return false;
        }
        if (!b.save_wayfire(&err))
        {
            if (status)
            {
                status->set_text(err.empty() ? "We couldn't save your workspace grid layout." : err);
            }
            return false;
        }
    }
    auto fi = bg_fill->get_selected();
    std::string fill = (fi < fill_values.size()) ? fill_values[fi] : "fill_and_crop";
    std::map<std::string, std::string> bg;
    bg["image"] = bg_image->get_text();
    bg["fill_mode"] = fill;
    bg["randomize"] = bg_random->get_active() ? "true" : "false";
    std::string err;
    if (!wf_shell::settings_save_section("background", bg, &err))
    {
        if (status)
        {
            status->set_text("We couldn't update your wallpaper: " + err);
        }
        return false;
    }
    if (status)
    {
        if (workspace_changed)
        {
            status->set_text(
                "✨ Wallpaper updated! Please log out and back in to see your new workspace grid.");
        } else
        {
            status->set_text("✨ Wallpaper updated successfully!");
        }
    }
    return true;
}

void DesktopPage::refresh_wallpaper_previews()
{
    // Clear previous buttons
    for (auto btn : wallpaper_buttons)
    {
        wallpaper_flow->remove(*btn);
    }
    wallpaper_buttons.clear();

    std::string bg_dir = std::string(RESOURCEDIR) + "/backgrounds";
    std::vector<std::string> paths;
    std::error_code ec;
    if (std::filesystem::is_directory(bg_dir, ec) && !ec)
    {
        for (auto& p : std::filesystem::directory_iterator(bg_dir, ec))
        {
            if (ec) break;
            if (p.is_regular_file())
            {
                auto ext = p.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                {
                    paths.push_back(p.path().string());
                }
            }
        }
    }

    std::string cur_path = bg_image->get_text();
    if (!cur_path.empty())
    {
        bool found = false;
        for (const auto& p : paths)
        {
            if (p == cur_path)
            {
                found = true;
                break;
            }
        }
        if (!found && std::filesystem::exists(cur_path, ec) && !ec)
        {
            paths.push_back(cur_path);
        }
    }

    // Sort to keep order consistent
    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths)
    {
        auto btn = Gtk::make_managed<Gtk::Button>();
        btn->add_css_class("wallpaper-card");
        if (path == cur_path)
        {
            btn->add_css_class("selected-wallpaper");
        }

        auto card_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card_box->set_margin(4);

        auto pic = Gtk::make_managed<Gtk::Picture>();
        pic->set_filename(path);
        pic->set_content_fit(Gtk::ContentFit::COVER);
        pic->set_size_request(130, 80);
        card_box->append(*pic);

        auto stem = std::filesystem::path(path).stem().string();
        std::replace(stem.begin(), stem.end(), '-', ' ');
        std::replace(stem.begin(), stem.end(), '_', ' ');
        if (!stem.empty())
        {
            stem[0] = std::toupper(stem[0]);
        }
        auto lbl = Gtk::make_managed<Gtk::Label>(stem);
        lbl->add_css_class("wallpaper-title");
        card_box->append(*lbl);

        btn->set_child(*card_box);
        btn->set_size_request(140, 110);

        btn->signal_clicked().connect([this, path] () {
            bg_image->set_text(path);
        });

        wallpaper_flow->append(*btn);
        wallpaper_buttons.push_back(btn);
    }

    // Special custom browse card at the end
    auto custom_btn = Gtk::make_managed<Gtk::Button>();
    custom_btn->add_css_class("wallpaper-card");
    custom_btn->add_css_class("wallpaper-custom-add");
    custom_btn->set_size_request(140, 110);

    auto add_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    add_content->set_valign(Gtk::Align::CENTER);
    add_content->set_halign(Gtk::Align::CENTER);

    auto plus_lbl = Gtk::make_managed<Gtk::Label>("➕");
    auto txt_lbl = Gtk::make_managed<Gtk::Label>("Browse...");
    txt_lbl->add_css_class("wallpaper-title");
    add_content->append(*plus_lbl);
    add_content->append(*txt_lbl);

    custom_btn->set_child(*add_content);
    custom_btn->signal_clicked().connect([this] () { on_browse(); });

    wallpaper_flow->append(*custom_btn);
    wallpaper_buttons.push_back(custom_btn);
}

void DesktopPage::fetch_online_feed()
{
    namespace fs = std::filesystem;
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    std::string cache_dir = home + "/.cache/wf-shell/wallpapers";
    std::string meta_path = cache_dir + "/metadata.json";
    
    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    
    // 1. Read cache first
    std::string cached_json = "";
    if (fs::exists(meta_path, ec))
    {
        std::ifstream in(meta_path);
        if (in)
        {
            std::ostringstream oss;
            oss << in.rdbuf();
            cached_json = oss.str();
        }
    }
    
    auto parse_and_populate = [this] (const std::string& json_text) -> bool {
        wf::json_t root;
        auto err = wf::json_t::parse_string(json_text, root);
        if (err || !root.is_array())
        {
            return false;
        }
        
        online_images.clear();
        for (size_t i = 0; i < root.size(); ++i)
        {
            auto item = root[i];
            if (item.is_object() && item.has_member("id") && item.has_member("author") && item.has_member("download_url"))
            {
                OnlineImage img;
                img.id = item["id"].as_string();
                img.author = item["author"].as_string();
                img.download_url = item["download_url"].as_string();
                if (item.has_member("thumb_url") && item["thumb_url"].is_string())
                {
                    img.thumb_url = item["thumb_url"].as_string();
                }
                online_images.push_back(img);
            }
        }
        return true;
    };
    
    if (!cached_json.empty())
    {
        if (parse_and_populate(cached_json))
        {
            update_online_grid(wallpaper_search ? wallpaper_search->get_text() : "");
        }
    }
    
    // 2. Fetch fresh feeds asynchronously (Bing Daily + Picsum Photos)
    std::thread([this, meta_path] () {
        std::vector<OnlineImage> fetched;
        
        // 2a. Fetch Bing daily wallpapers
        {
            std::string bing_url = "https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=8&mkt=en-US";
            std::string cmd = "curl -s -L -m 4 \"" + bing_url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - \"" + bing_url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t root;
                if (!result.empty() && !wf::json_t::parse_string(result, root))
                {
                    if (root.is_object() && root.has_member("images") && root["images"].is_array())
                    {
                        auto arr = root["images"];
                        for (size_t i = 0; i < arr.size(); ++i)
                        {
                            auto item = arr[i];
                            if (item.is_object() && item.has_member("url") && item.has_member("copyright"))
                            {
                                std::string url = item["url"].as_string();
                                std::string cop = item["copyright"].as_string();
                                std::string title = item.has_member("title") ? item["title"].as_string() : "";
                                
                                OnlineImage img;
                                img.id = "bing_" + std::to_string(i);
                                img.author = title.empty() ? cop : title + " (" + cop + ")";
                                img.download_url = "https://www.bing.com" + url;
                                if (item.has_member("urlbase"))
                                {
                                    img.thumb_url = "https://www.bing.com" + item["urlbase"].as_string() + "_320x180.jpg";
                                }
                                else
                                {
                                    img.thumb_url = img.download_url;
                                }
                                fetched.push_back(img);
                            }
                        }
                    }
                }
            }
        }
        
        // 2b. Fetch Picsum list
        {
            std::string picsum_url = "https://picsum.photos/v2/list?limit=60";
            std::string cmd = "curl -s -L -m 4 \"" + picsum_url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - \"" + picsum_url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t root;
                if (!result.empty() && !wf::json_t::parse_string(result, root))
                {
                    if (root.is_array())
                    {
                        for (size_t i = 0; i < root.size(); ++i)
                        {
                            auto item = root[i];
                            if (item.is_object() && item.has_member("id") && item.has_member("author") && item.has_member("download_url"))
                            {
                                OnlineImage img;
                                img.id = "picsum_" + item["id"].as_string();
                                img.author = item["author"].as_string();
                                img.download_url = item["download_url"].as_string();
                                img.thumb_url = "";
                                fetched.push_back(img);
                            }
                        }
                    }
                }
            }
        }
        
        // 2c. Fetch dharmx/walls GitHub repository tree
        {
            std::string github_url = "https://api.github.com/repos/dharmx/walls/git/trees/main?recursive=1";
            std::string cmd = "curl -s -L -H \"User-Agent: wf-settings\" -m 4 \"" + github_url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - --user-agent=\"wf-settings\" \"" + github_url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t gh_root;
                if (!result.empty() && !wf::json_t::parse_string(result, gh_root))
                {
                    if (gh_root.is_object() && gh_root.has_member("tree") && gh_root["tree"].is_array())
                    {
                        auto tree = gh_root["tree"];
                        int walls_count = 0;
                        for (size_t i = 0; i < tree.size(); ++i)
                        {
                            auto node = tree[i];
                            if (node.is_object() && node.has_member("path") && node["path"].is_string() &&
                                node.has_member("type") && node["type"].as_string() == "blob")
                            {
                                std::string path = node["path"].as_string();
                                std::string lower_path = path;
                                std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
                                
                                if (lower_path.find(".png") != std::string::npos || 
                                    lower_path.find(".jpg") != std::string::npos || 
                                    lower_path.find(".jpeg") != std::string::npos)
                                {
                                    std::string category = "dharmx";
                                    size_t slash = path.find('/');
                                    if (slash != std::string::npos)
                                    {
                                        category = "dharmx (" + path.substr(0, slash) + ")";
                                    }
                                    
                                    std::string filename = path;
                                    size_t last_slash = path.find_last_of('/');
                                    if (last_slash != std::string::npos)
                                    {
                                        filename = path.substr(last_slash + 1);
                                    }
                                    size_t dot = filename.find_last_of('.');
                                    if (dot != std::string::npos)
                                    {
                                        filename = filename.substr(0, dot);
                                    }
                                    std::replace(filename.begin(), filename.end(), '_', ' ');
                                    std::replace(filename.begin(), filename.end(), '-', ' ');
                                    if (!filename.empty())
                                    {
                                        filename[0] = std::toupper(filename[0]);
                                    }
                                    
                                    OnlineImage img;
                                    img.id = "dharmx_" + std::to_string(walls_count++);
                                    img.author = filename + " — " + category;
                                    img.download_url = "https://raw.githubusercontent.com/dharmx/walls/main/" + path;
                                    img.thumb_url = img.download_url;
                                    fetched.push_back(img);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2d. Fetch OneDark wallpapers GitHub repository tree
        {
            std::string onedark_url = "https://api.github.com/repos/Narmis-E/onedark-wallpapers/git/trees/main?recursive=1";
            std::string cmd = "curl -s -L -H \"User-Agent: wf-settings\" -m 4 \"" + onedark_url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - --user-agent=\"wf-settings\" \"" + onedark_url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t gh_root;
                if (!result.empty() && !wf::json_t::parse_string(result, gh_root))
                {
                    if (gh_root.is_object() && gh_root.has_member("tree") && gh_root["tree"].is_array())
                    {
                        auto tree = gh_root["tree"];
                        int walls_count = 0;
                        for (size_t i = 0; i < tree.size(); ++i)
                        {
                            auto node = tree[i];
                            if (node.is_object() && node.has_member("path") && node["path"].is_string() &&
                                node.has_member("type") && node["type"].as_string() == "blob")
                            {
                                std::string path = node["path"].as_string();
                                std::string lower_path = path;
                                std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
                                
                                if (lower_path.find(".png") != std::string::npos || 
                                    lower_path.find(".jpg") != std::string::npos || 
                                    lower_path.find(".jpeg") != std::string::npos)
                                {
                                    std::string category = "onedark";
                                    size_t slash = path.find('/');
                                    if (slash != std::string::npos)
                                    {
                                        category = "onedark (" + path.substr(0, slash) + ")";
                                    }
                                    
                                    std::string filename = path;
                                    size_t last_slash = path.find_last_of('/');
                                    if (last_slash != std::string::npos)
                                    {
                                        filename = path.substr(last_slash + 1);
                                    }
                                    size_t dot = filename.find_last_of('.');
                                    if (dot != std::string::npos)
                                    {
                                        filename = filename.substr(0, dot);
                                    }
                                    std::replace(filename.begin(), filename.end(), '_', ' ');
                                    std::replace(filename.begin(), filename.end(), '-', ' ');
                                    if (!filename.empty())
                                    {
                                        filename[0] = std::toupper(filename[0]);
                                    }
                                    
                                    OnlineImage img;
                                    img.id = "onedark_" + std::to_string(walls_count++);
                                    img.author = filename + " — " + category;
                                    img.download_url = "https://raw.githubusercontent.com/Narmis-E/onedark-wallpapers/main/" + path;
                                    img.thumb_url = img.download_url;
                                    fetched.push_back(img);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2e. Fetch Wallhaven collection variety
        {
            std::string wallhaven_url = "https://wallhaven.cc/api/v1/collections/lewdpatriot/935888?page=1";
            std::string cmd = "curl -s -L -m 4 \"" + wallhaven_url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - \"" + wallhaven_url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t wh_root;
                if (!result.empty() && !wf::json_t::parse_string(result, wh_root))
                {
                    if (wh_root.is_object() && wh_root.has_member("data") && wh_root["data"].is_array())
                    {
                        auto data = wh_root["data"];
                        for (size_t i = 0; i < data.size(); ++i)
                        {
                            auto item = data[i];
                            if (item.is_object() && item.has_member("id") && item.has_member("path") && item.has_member("thumbs"))
                            {
                                std::string id = item["id"].as_string();
                                std::string path = item["path"].as_string();
                                std::string category = item.has_member("category") ? item["category"].as_string() : "general";
                                std::string resolution = item.has_member("resolution") ? item["resolution"].as_string() : "";
                                
                                auto thumbs = item["thumbs"];
                                std::string thumb = (thumbs.is_object() && thumbs.has_member("small")) ? thumbs["small"].as_string() : path;
                                
                                OnlineImage img;
                                img.id = "wallhaven_" + id;
                                img.author = "Wallhaven variety (" + category + (resolution.empty() ? "" : " " + resolution) + ")";
                                img.download_url = path;
                                img.thumb_url = thumb;
                                fetched.push_back(img);
                            }
                        }
                    }
                }
            }
        }
        
        // 3. Serialize combined feed to metadata.json in unified format
        if (!fetched.empty())
        {
            std::ostringstream o;
            o << "[\n";
            for (size_t i = 0; i < fetched.size(); ++i)
            {
                o << "  {\n";
                o << "    \"id\": \"" << escape_json_str(fetched[i].id) << "\",\n";
                o << "    \"author\": \"" << escape_json_str(fetched[i].author) << "\",\n";
                o << "    \"download_url\": \"" << escape_json_str(fetched[i].download_url) << "\",\n";
                o << "    \"thumb_url\": \"" << escape_json_str(fetched[i].thumb_url) << "\"\n";
                o << "  }" << (i + 1 < fetched.size() ? "," : "") << "\n";
            }
            o << "]";
            
            std::string serialized = o.str();
            std::ofstream out(meta_path, std::ios::trunc);
            if (out)
            {
                out << serialized;
            }
            
            Glib::signal_idle().connect_once([this, fetched] () {
                online_images = fetched;
                update_online_grid(wallpaper_search ? wallpaper_search->get_text() : "");
            });
        }
    }).detach();
}

void DesktopPage::update_online_grid(const std::string& query)
{
    namespace fs = std::filesystem;
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    std::string cache_dir = home + "/.cache/wf-shell/wallpapers";

    for (auto btn : online_buttons)
    {
        online_flow->remove(*btn);
    }
    online_buttons.clear();

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    int count = 0;
    for (const auto& img : online_images)
    {
        if (count >= 40) break;

        std::string author_lower = img.author;
        std::transform(author_lower.begin(), author_lower.end(), author_lower.begin(), ::tolower);

        if (!lower_query.empty() && author_lower.find(lower_query) == std::string::npos)
        {
            continue;
        }

        count++;

        auto btn = Gtk::make_managed<Gtk::Button>();
        btn->add_css_class("wallpaper-card");
        btn->set_size_request(140, 110);

        auto card_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card_box->set_margin(4);

        auto pic = Gtk::make_managed<Gtk::Picture>();
        pic->set_content_fit(Gtk::ContentFit::COVER);
        pic->set_size_request(130, 80);

        std::string thumb_path = cache_dir + "/thumb_" + img.id + ".jpg";
        std::error_code ec;
        if (fs::exists(thumb_path, ec))
        {
            pic->set_filename(thumb_path);
        }
        else
        {
            std::thread([thumb_path, img, pic] () {
                std::string url = img.thumb_url;
                if (url.empty())
                {
                    std::string raw_id = img.id;
                    if (raw_id.rfind("picsum_", 0) == 0)
                    {
                        raw_id = raw_id.substr(7);
                    }
                    url = "https://picsum.photos/id/" + raw_id + "/200/120";
                }
                
                std::string cmd = "curl -s -L -o \"" + thumb_path + "\" \"" + url + "\"";
                if (system("which curl >/dev/null 2>&1") != 0)
                {
                    cmd = "fetch -q -o \"" + thumb_path + "\" \"" + url + "\"";
                }
                int res = system(cmd.c_str());
                if (res == 0)
                {
                    Glib::signal_idle().connect_once([thumb_path, pic] () {
                        pic->set_filename(thumb_path);
                    });
                }
            }).detach();
        }

        card_box->append(*pic);

        auto lbl = Gtk::make_managed<Gtk::Label>(img.author);
        lbl->add_css_class("wallpaper-title");
        card_box->append(*lbl);

        btn->set_child(*card_box);

        btn->signal_clicked().connect([this, img] () {
            download_wallpaper(img.id, img.download_url);
        });

        online_flow->append(*btn);
        online_buttons.push_back(btn);
    }

    if (online_status_lbl)
    {
        if (count == 0)
        {
            online_status_lbl->set_text(online_images.empty()
                ? "Feed offline. Showing local wallpapers."
                : "No matching online wallpapers found.");
        }
        else
        {
            online_status_lbl->set_text("Showing " + std::to_string(count) + " online wallpapers (Bing Daily + Picsum).");
        }
    }

    if (lower_query.length() >= 3)
    {
        std::thread([this, lower_query] () {
            std::string encoded = url_encode(lower_query);
            std::string url = "https://wallhaven.cc/api/v1/search?q=" + encoded + "&purity=100&sorting=views";
            std::string cmd = "curl -s -L -m 4 \"" + url + "\"";
            if (system("which curl >/dev/null 2>&1") != 0)
            {
                cmd = "fetch -T 4 -q -o - \"" + url + "\"";
            }
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buffer[256];
                std::string result = "";
                while (!feof(pipe))
                {
                    if (fgets(buffer, 256, pipe) != NULL)
                        result += buffer;
                }
                pclose(pipe);
                
                wf::json_t wh_root;
                if (!result.empty() && !wf::json_t::parse_string(result, wh_root))
                {
                    if (wh_root.is_object() && wh_root.has_member("data") && wh_root["data"].is_array())
                    {
                        auto data = wh_root["data"];
                        std::vector<OnlineImage> searched_images;
                        for (size_t i = 0; i < data.size(); ++i)
                        {
                            auto item = data[i];
                            if (item.is_object() && item.has_member("id") && item.has_member("path") && item.has_member("thumbs"))
                            {
                                std::string id = item["id"].as_string();
                                std::string path = item["path"].as_string();
                                std::string category = item.has_member("category") ? item["category"].as_string() : "general";
                                std::string resolution = item.has_member("resolution") ? item["resolution"].as_string() : "";
                                
                                auto thumbs = item["thumbs"];
                                std::string thumb = (thumbs.is_object() && thumbs.has_member("small")) ? thumbs["small"].as_string() : path;
                                
                                OnlineImage img;
                                img.id = "wallhaven_search_" + id;
                                img.author = "Wallhaven (" + category + (resolution.empty() ? "" : " " + resolution) + ")";
                                img.download_url = path;
                                img.thumb_url = thumb;
                                searched_images.push_back(img);
                            }
                        }
                        
                        Glib::signal_idle().connect_once([this, lower_query, searched_images] () {
                            std::string active_query = wallpaper_search ? wallpaper_search->get_text() : "";
                            std::string active_lower = active_query;
                            std::transform(active_lower.begin(), active_lower.end(), active_lower.begin(), ::tolower);
                            if (active_lower != lower_query) return;
                            
                            append_search_results_to_grid(searched_images);
                        });
                    }
                }
            }
        }).detach();
    }
}

void DesktopPage::append_search_results_to_grid(const std::vector<OnlineImage>& list)
{
    namespace fs = std::filesystem;
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    std::string cache_dir = home + "/.cache/wf-shell/wallpapers";

    int count = online_buttons.size();
    int added = 0;
    for (const auto& img : list)
    {
        if (count >= 40) break;

        count++;
        added++;

        auto btn = Gtk::make_managed<Gtk::Button>();
        btn->add_css_class("wallpaper-card");
        btn->set_size_request(140, 110);

        auto card_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card_box->set_margin(4);

        auto pic = Gtk::make_managed<Gtk::Picture>();
        pic->set_content_fit(Gtk::ContentFit::COVER);
        pic->set_size_request(130, 80);

        std::string thumb_path = cache_dir + "/thumb_" + img.id + ".jpg";
        std::error_code ec;
        if (fs::exists(thumb_path, ec))
        {
            pic->set_filename(thumb_path);
        }
        else
        {
            std::thread([thumb_path, img, pic] () {
                std::string url = img.thumb_url;
                std::string cmd = "curl -s -L -o \"" + thumb_path + "\" \"" + url + "\"";
                if (system("which curl >/dev/null 2>&1") != 0)
                {
                    cmd = "fetch -q -o \"" + thumb_path + "\" \"" + url + "\"";
                }
                int res = system(cmd.c_str());
                if (res == 0)
                {
                    Glib::signal_idle().connect_once([thumb_path, pic] () {
                        pic->set_filename(thumb_path);
                    });
                }
            }).detach();
        }

        card_box->append(*pic);

        auto lbl = Gtk::make_managed<Gtk::Label>(img.author);
        lbl->add_css_class("wallpaper-title");
        card_box->append(*lbl);

        btn->set_child(*card_box);

        btn->signal_clicked().connect([this, img] () {
            download_wallpaper(img.id, img.download_url);
        });

        online_flow->append(*btn);
        online_buttons.push_back(btn);
    }

    if (online_status_lbl && added > 0)
    {
        std::string cur = online_status_lbl->get_text();
        online_status_lbl->set_text(cur + " + " + std::to_string(added) + " global Wallhaven search matches.");
    }
}

void DesktopPage::download_wallpaper(const std::string& id, const std::string& download_url)
{
    namespace fs = std::filesystem;
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    std::string cache_dir = home + "/.cache/wf-shell/wallpapers";
    std::string target_path = cache_dir + "/wallpaper_" + id + ".jpg";

    if (status)
    {
        status->set_text("Downloading wallpaper in background...");
    }

    std::thread([this, id, download_url, target_path, cache_dir] () {
        enforce_cache_limit(cache_dir);

        std::string url = download_url;
        if (id.rfind("picsum_", 0) == 0)
        {
            std::string raw_id = id.substr(7);
            url = "https://picsum.photos/id/" + raw_id + "/1920/1080";
        }
        
        std::string cmd = "curl -s -L -o \"" + target_path + "\" \"" + url + "\"";
        if (system("which curl >/dev/null 2>&1") != 0)
        {
            cmd = "fetch -q -o \"" + target_path + "\" \"" + url + "\"";
        }
        int res = system(cmd.c_str());

        Glib::signal_idle().connect_once([this, res, target_path] () {
            if (res == 0)
            {
                bg_image->set_text(target_path);
                save(nullptr);
                refresh_wallpaper_previews();
                if (status)
                {
                    status->set_text("✨ Online wallpaper applied successfully!");
                }
            }
            else
            {
                if (status)
                {
                    status->set_text("We couldn't download this wallpaper. Please check your internet connection.");
                }
            }
        });
    }).detach();
}

} // namespace wf_settings
