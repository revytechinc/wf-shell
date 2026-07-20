#pragma once

#include <gtkmm.h>
#include "shell-json-config.hpp"

namespace wf_settings
{

/**
 * AI / MCP servers — config scaffold for agent tooling.
 * Runtime launch of MCP servers comes later; Settings owns enablement + metadata.
 */
class McpPage : public Gtk::Box
{
  public:
    McpPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);

  private:
    void on_apply();
    void rebuild_list();

    Gtk::CheckButton *mcp_enabled = nullptr;
    Gtk::ListBox *list = nullptr;
    Gtk::Button *apply_btn = nullptr;
    Gtk::Button *reload_btn = nullptr;
    Gtk::Label *status = nullptr;
    Gtk::Label *help = nullptr;

    wf_shell::ShellJsonConfig cfg;
    std::vector<Gtk::CheckButton*> server_checks;
};

} // namespace wf_settings
