#pragma once

#include <gtkmm.h>
#include <string>

#include "config-backend.hpp"

namespace wf_settings
{

/**
 * Options for a single Wayfire or Shell plugin/section.
 * One page in the main stack — never a multi-plugin dump.
 */
class PluginPage : public Gtk::Box
{
  public:
    PluginPage(ConfigDomain domain, std::string section, std::string title,
        std::string blurb, std::string category);

    void refresh();
    void set_status_target(Gtk::Label *status_label);

    ConfigDomain domain() const { return domain_; }
    const std::string& section() const { return section_; }
    const std::string& title() const { return title_; }
    const std::string& category() const { return category_; }
    const std::string& blurb() const { return blurb_; }

    /** Stack child id: "w:animate" or "s:panel" */
    static std::string stack_id(ConfigDomain dom, const std::string& section);

  private:
    void rebuild();
    void clear_options();
    void update_enabled_ui();

    ConfigDomain domain_;
    std::string section_;
    std::string title_;
    std::string blurb_;
    std::string category_;

    Gtk::Label *title_lbl = nullptr;
    Gtk::Label *blurb_lbl = nullptr;
    Gtk::CheckButton *enabled_chk = nullptr;
    Gtk::Box *opt_box = nullptr;
    Gtk::Label *status = nullptr;
    bool filling = false;
};

} // namespace wf_settings
