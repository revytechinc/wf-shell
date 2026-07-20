#include "mcp-page.hpp"

#include <iostream>

namespace wf_settings
{

McpPage::McpPage() :
    Gtk::Box(Gtk::Orientation::VERTICAL, 10)
{
    set_margin(16);

    auto title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>AI &amp; MCP servers</b>");
    title->set_halign(Gtk::Align::START);
    append(*title);

    help = Gtk::make_managed<Gtk::Label>();
    help->set_wrap(true);
    help->set_halign(Gtk::Align::START);
    help->add_css_class("dim-label");
    help->set_text(
        "Model Context Protocol servers for desktop agents. "
        "Stored in ~/.config/wf-shell/config.json under \"mcp\". "
        "Enable when the desktop MCP runtime is integrated — settings persist now.");
    append(*help);

    mcp_enabled = Gtk::make_managed<Gtk::CheckButton>("Enable MCP integration (global)");
    mcp_enabled->signal_toggled().connect([this] () { save(nullptr); });
    append(*mcp_enabled);

    auto list_title = Gtk::make_managed<Gtk::Label>("Servers (discovered from config.json)");
    list_title->set_halign(Gtk::Align::START);
    list_title->set_margin_top(8);
    append(*list_title);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_min_content_height(180);
    list = Gtk::make_managed<Gtk::ListBox>();
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*list);
    append(*scroll);

    refresh();
}

void McpPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

void McpPage::rebuild_list()
{
    while (auto *row = list->get_row_at_index(0))
    {
        list->remove(*row);
    }
    server_checks.clear();

    for (auto& s : cfg.mcp_servers)
    {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin(8);
        auto chk = Gtk::make_managed<Gtk::CheckButton>(
            s.name.empty() ? s.id : (s.name + " (" + s.id + ")"));
        chk->set_active(s.enabled);
        chk->signal_toggled().connect([this] () { save(nullptr); });
        auto meta = Gtk::make_managed<Gtk::Label>();
        meta->set_halign(Gtk::Align::START);
        meta->add_css_class("dim-label");
        std::string m = s.transport + " · " + s.command;
        if (!s.notes.empty())
        {
            m += "\n" + s.notes;
        }
        meta->set_text(m);
        meta->set_wrap(true);
        box->append(*chk);
        box->append(*meta);
        row->set_child(*box);
        list->append(*row);
        server_checks.push_back(chk);
    }

    if (cfg.mcp_servers.empty())
    {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto l = Gtk::make_managed<Gtk::Label>("No MCP servers in config — defaults will be created on save.");
        l->set_margin(12);
        row->set_child(*l);
        list->append(*row);
    }
}

void McpPage::refresh()
{
    auto path = wf_shell::shell_json_config_path();
    std::string err;
    cfg = wf_shell::ShellJsonConfig{};
    if (!wf_shell::load_shell_json_config(path, cfg, &err))
    {
        cfg.version = 1;
        cfg.path = path;
        wf_shell::ensure_default_mcp_stub(cfg);
        if (status)
        {
            status->set_text("No config.json yet — showing MCP stubs (will create on save).");
        }
    } else if (status)
    {
        status->set_text("Loaded MCP config from " + path);
    }
    if (cfg.mcp_servers.empty())
    {
        wf_shell::ensure_default_mcp_stub(cfg);
    }
    mcp_enabled->set_active(cfg.mcp_enabled);
    rebuild_list();
}

bool McpPage::save(std::string *error)
{
    cfg.mcp_enabled = mcp_enabled->get_active();
    for (size_t i = 0; i < server_checks.size() && i < cfg.mcp_servers.size(); ++i)
    {
        cfg.mcp_servers[i].enabled = server_checks[i]->get_active();
    }
    if (cfg.mcp_servers.empty())
    {
        wf_shell::ensure_default_mcp_stub(cfg);
    }
    auto path = wf_shell::shell_json_config_path();
    /* Preserve other sections if file exists */
    wf_shell::ShellJsonConfig existing;
    std::string lerr;
    if (wf_shell::load_shell_json_config(path, existing, &lerr))
    {
        existing.mcp_enabled = cfg.mcp_enabled;
        existing.mcp_servers = cfg.mcp_servers;
        cfg = existing;
    }
    cfg.path = path;
    std::string err;
    if (!wf_shell::save_shell_json_config(path, cfg, &err))
    {
        if (status)
        {
            status->set_text("We couldn't save the MCP configuration: " + err);
        }
        return false;
    }
    if (status)
    {
        status->set_text("✨ MCP helper configuration updated successfully!");
    }
    return true;
}

} // namespace wf_settings
