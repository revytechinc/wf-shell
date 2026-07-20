#include "desktop-page.hpp"

#include "config-backend.hpp"
#include "ini-file.hpp"
#include "shell-json-config.hpp"
#include "ux-labels.hpp"

#include <cstdlib>
#include <map>
#include <string>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace wf_settings
{

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

} // namespace wf_settings
