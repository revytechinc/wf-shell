#pragma once

#include <gtkmm.h>
#include <string>
#include <vector>

namespace wf_settings
{

/**
 * Session power actions — auto-discovered via WFPowerController factory.
 * Mom sees "Shut down" as Ready / Not available, not "systemctl poweroff".
 */
class SessionPage : public Gtk::Box
{
  public:
    SessionPage();
    void refresh();
    void set_status_target(Gtk::Label *status_label);
    bool save(std::string *error = nullptr);

  private:
    void rebuild_rows();
    std::string shell_ini() const;

    struct ActionRow
    {
        std::string config_key;   /* panel/shutdown_command */
        std::string title;        /* Shut down */
        std::string discovered;   /* actual command from factory */
        bool available = false;
        bool permitted = false;
        Gtk::Label *status_lbl = nullptr;
        Gtk::Label *detail_lbl = nullptr;
        Gtk::CheckButton *use_chk = nullptr;
    };

    std::vector<ActionRow> rows;
    Gtk::Box *list_box = nullptr;
    Gtk::Label *status = nullptr;
    Gtk::Label *help = nullptr;
};

} // namespace wf_settings
