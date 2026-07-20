#include "plugin-page.hpp"

#include "ux-labels.hpp"

#include <wayfire/config/section.hpp>
#include <wayfire/config/xml.hpp>
#include <libxml/tree.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>
#include <vector>

namespace wf_settings
{
namespace
{

const std::set<std::string> kCoreAlways = {"core", "input", "workarounds", "output"};

bool is_truthy(const std::string& v)
{
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

bool looks_bool(const std::string& v)
{
    return v == "true" || v == "false" || v == "0" || v == "1";
}

bool looks_int(const std::string& v)
{
    if (v.empty())
    {
        return false;
    }
    size_t i = (v[0] == '-' || v[0] == '+') ? 1 : 0;
    if (i >= v.size())
    {
        return false;
    }
    for (; i < v.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(v[i])))
        {
            return false;
        }
    }
    return true;
}

bool looks_double(const std::string& v)
{
    if (v.empty())
    {
        return false;
    }
    bool dot = false;
    size_t i = (v[0] == '-' || v[0] == '+') ? 1 : 0;
    for (; i < v.size(); ++i)
    {
        if (v[i] == '.')
        {
            if (dot)
            {
                return false;
            }
            dot = true;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(v[i])))
        {
            return false;
        }
    }
    return dot;
}

std::vector<std::string> split_plugins(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == ',')
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        } else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        out.push_back(cur);
    }
    return out;
}

std::string join_plugins(const std::vector<std::string>& parts)
{
    std::ostringstream o;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
        {
            o << ' ';
        }
        o << parts[i];
    }
    return o.str();
}

bool plugin_in_list(const std::string& plugins, const std::string& name)
{
    for (const auto& p : split_plugins(plugins))
    {
        if (p == name)
        {
            return true;
        }
    }
    return false;
}

std::string xml_attr(xmlNode *node, const char *attr)
{
    if (!node)
    {
        return {};
    }
    xmlChar *a = xmlGetProp(node, reinterpret_cast<const xmlChar*>(attr));
    if (!a)
    {
        return {};
    }
    std::string s = reinterpret_cast<char*>(a);
    xmlFree(a);
    return s;
}

std::string xml_child_text(xmlNode *node, const char *name)
{
    if (!node)
    {
        return {};
    }
    for (xmlNode *ch = node->children; ch; ch = ch->next)
    {
        if (ch->type != XML_ELEMENT_NODE || !ch->name)
        {
            continue;
        }
        std::string n = reinterpret_cast<const char*>(ch->name);
        if (n == name || n == std::string("_") + name)
        {
            xmlChar *c = xmlNodeGetContent(ch);
            if (!c)
            {
                return {};
            }
            std::string s = reinterpret_cast<char*>(c);
            xmlFree(c);
            return s;
        }
    }
    return {};
}

void collect_enum_choices(xmlNode *opt_node,
    std::vector<std::pair<std::string, std::string>>& out)
{
    if (!opt_node)
    {
        return;
    }
    for (xmlNode *ch = opt_node->children; ch; ch = ch->next)
    {
        if (ch->type != XML_ELEMENT_NODE || !ch->name)
        {
            continue;
        }
        if (std::string(reinterpret_cast<const char*>(ch->name)) != "desc")
        {
            continue;
        }
        std::string value, label;
        for (xmlNode *d = ch->children; d; d = d->next)
        {
            if (d->type != XML_ELEMENT_NODE || !d->name)
            {
                continue;
            }
            std::string dn = reinterpret_cast<const char*>(d->name);
            xmlChar *c = xmlNodeGetContent(d);
            if (!c)
            {
                continue;
            }
            std::string t = reinterpret_cast<char*>(c);
            xmlFree(c);
            if (dn == "value")
            {
                value = t;
            } else if (dn == "name" || dn == "_name")
            {
                label = t;
            }
        }
        if (!value.empty())
        {
            out.emplace_back(value, label.empty() ? value : label);
        }
    }
}

void clear_box(Gtk::Box *box)
{
    auto kids = box->get_children();
    for (auto *c : kids)
    {
        box->remove(*c);
    }
}

/** Wayfire color: "r g b a" floats 0–1 → Gdk::RGBA */
bool parse_wf_color(const std::string& s, Gdk::RGBA& out)
{
    double r = 0, g = 0, b = 0, a = 1;
    int n = std::sscanf(s.c_str(), "%lf %lf %lf %lf", &r, &g, &b, &a);
    if (n < 3)
    {
        return false;
    }
    if (n == 3)
    {
        a = 1.0;
    }
    out.set_rgba(r, g, b, a);
    return true;
}

std::string format_wf_color(const Gdk::RGBA& c)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4f %.4f %.4f %.4f",
        c.get_red(), c.get_green(), c.get_blue(), c.get_alpha());
    return buf;
}

} // namespace

std::string PluginPage::stack_id(ConfigDomain dom, const std::string& section)
{
    return (dom == ConfigDomain::Shell ? "s:" : "w:") + section;
}

PluginPage::PluginPage(ConfigDomain domain, std::string section, std::string title,
    std::string blurb, std::string category) :
    Gtk::Box(Gtk::Orientation::VERTICAL, 8),
    domain_(domain),
    section_(std::move(section)),
    title_(std::move(title)),
    blurb_(std::move(blurb)),
    category_(std::move(category))
{
    set_margin(16);

    title_lbl = Gtk::make_managed<Gtk::Label>();
    title_lbl->set_halign(Gtk::Align::START);
    title_lbl->set_hexpand(true);
    title_lbl->set_wrap(true);
    append(*title_lbl);

    blurb_lbl = Gtk::make_managed<Gtk::Label>();
    blurb_lbl->set_wrap(true);
    blurb_lbl->set_halign(Gtk::Align::START);
    blurb_lbl->add_css_class("dim-label");
    append(*blurb_lbl);

    enabled_chk = Gtk::make_managed<Gtk::CheckButton>("Enabled (loaded in core/plugins)");
    enabled_chk->set_visible(false);
    append(*enabled_chk);

    enabled_chk->signal_toggled().connect([this] () {
        if (filling || domain_ != ConfigDomain::Wayfire)
        {
            return;
        }
        if (kCoreAlways.count(section_))
        {
            return;
        }
        auto opt = ConfigBackend::instance().wayfire.get_option("core/plugins");
        if (!opt)
        {
            return;
        }
        auto list = split_plugins(opt->get_value_str());
        bool on = enabled_chk->get_active();
        bool has = plugin_in_list(opt->get_value_str(), section_);
        if (on && !has)
        {
            list.push_back(section_);
        } else if (!on && has)
        {
            list.erase(std::remove(list.begin(), list.end(), section_), list.end());
        }
        std::string err;
        if (!ConfigBackend::instance().set_wayfire_option("core", "plugins",
                join_plugins(list), &err))
        {
            if (status)
            {
                status->set_text(err.empty() ? "Could not change plugins list" : err);
            }
            return;
        }
        if (status)
        {
            status->set_text((on ? "Enabled " : "Disabled ") + section_ +
                " — changes apply immediately");
        }
    });

    opt_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    append(*opt_box);

    refresh();
}

void PluginPage::set_status_target(Gtk::Label *s)
{
    status = s;
}

void PluginPage::refresh()
{
    title_lbl->set_markup("<b>" + Glib::Markup::escape_text(title_) + "</b>"
        "  <span alpha=\"60%\">(" + Glib::Markup::escape_text(section_) +
        " · " + Glib::Markup::escape_text(category_) + ")</span>");
    blurb_lbl->set_text(blurb_.empty()
        ? ("Plugin section: " + section_)
        : blurb_);
    update_enabled_ui();
    rebuild();
}

void PluginPage::update_enabled_ui()
{
    filling = true;
    if (domain_ == ConfigDomain::Wayfire && !kCoreAlways.count(section_))
    {
        enabled_chk->set_visible(true);
        auto opt = ConfigBackend::instance().wayfire.get_option("core/plugins");
        enabled_chk->set_active(
            opt && plugin_in_list(opt->get_value_str(), section_));
    } else
    {
        enabled_chk->set_visible(false);
    }
    filling = false;
}

void PluginPage::clear_options()
{
    clear_box(opt_box);
}

void PluginPage::rebuild()
{
    clear_options();
    auto& b = ConfigBackend::instance();
    auto& mgr = (domain_ == ConfigDomain::Wayfire) ? b.wayfire : b.shell;
    auto section = mgr.get_section(section_);
    if (!section)
    {
        opt_box->append(*Gtk::make_managed<Gtk::Label>("Section missing from config."));
        return;
    }
    auto opts = section->get_registered_options();
    if (opts.empty())
    {
        opt_box->append(*Gtk::make_managed<Gtk::Label>("No options in this plugin."));
        return;
    }

    for (auto& opt : opts)
    {
        if (!opt)
        {
            continue;
        }

        xmlNode *onode = wf::config::xml::get_option_xml_node(opt);
        std::string short_label = xml_child_text(onode, "short");
        std::string long_label  = xml_child_text(onode, "long");
        std::string xml_type    = xml_attr(onode, "type");
        if (short_label.empty())
        {
            short_label = ux::humanize_token(opt->get_name());
        }

        auto frame = Gtk::make_managed<Gtk::Frame>();
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        col->set_margin(10);

        auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto nl = Gtk::make_managed<Gtk::Label>();
        nl->set_markup("<b>" + Glib::Markup::escape_text(short_label) + "</b>");
        nl->set_halign(Gtk::Align::START);
        nl->set_hexpand(true);
        nl->set_wrap(true);
        nl->set_tooltip_text(opt->get_name()); /* raw key only as tooltip */
        head->append(*nl);
        auto reset = Gtk::make_managed<Gtk::Button>("Reset");
        head->append(*reset);
        col->append(*head);

        if (!long_label.empty())
        {
            auto desc = Gtk::make_managed<Gtk::Label>(long_label);
            desc->set_wrap(true);
            desc->set_halign(Gtk::Align::START);
            desc->add_css_class("dim-label");
            col->append(*desc);
        }

        std::string val = opt->get_value_str();
        std::string full = section_ + "/" + opt->get_name();
        ConfigDomain dom = domain_;

        auto commit = [opt, dom, full, this] (const std::string& v) -> bool {
            auto slash = full.find('/');
            std::string sec = (slash == std::string::npos) ? full : full.substr(0, slash);
            std::string key = (slash == std::string::npos) ? full : full.substr(slash + 1);

            if (dom == ConfigDomain::Wayfire)
            {
                std::string err;
                /* Dirty-key only — never full wayfire.ini dump (crashes compositor) */
                if (!ConfigBackend::instance().set_wayfire_option(sec, key, v, &err))
                {
                    if (status)
                    {
                        status->set_text(err.empty() ? ("Rejected: " + full) : err);
                    }
                    return false;
                }
                /* Modeless (Apple HIG): commit dirty keys immediately. */
                if (!ConfigBackend::instance().save_wayfire(&err))
                {
                    if (status)
                    {
                        status->set_text(err.empty() ? "Could not write settings." : err);
                    }
                    return false;
                }
            } else
            {
                if (opt && !opt->set_value_str(v))
                {
                    if (status)
                    {
                        status->set_text("Rejected: " + full);
                    }
                    return false;
                }
                ConfigBackend::instance().save_shell_option(sec, key, v, nullptr);
            }
            if (status)
            {
                status->set_text("Updated.");
            }
            return true;
        };

        reset->signal_clicked().connect([opt, commit] () {
            opt->reset_to_default();
            commit(opt->get_value_str());
        });

        std::vector<std::pair<std::string, std::string>> choices;
        collect_enum_choices(onode, choices);
        for (auto& ch : choices)
        {
            if (ch.second == ch.first)
            {
                ch.second = ux::humanize_token(ch.first);
            }
        }

        const bool is_bool = (xml_type == "bool") || (xml_type.empty() && looks_bool(val));
        const bool is_int  = (xml_type == "int") ||
            (xml_type.empty() && looks_int(val) && !looks_bool(val));
        const bool is_dbl  = (xml_type == "double") ||
            (xml_type.empty() && looks_double(val));
        const bool is_color = (xml_type == "color");

        if (is_bool)
        {
            auto chk = Gtk::make_managed<Gtk::CheckButton>("On");
            chk->set_active(is_truthy(val));
            chk->signal_toggled().connect([chk, commit] () {
                commit(chk->get_active() ? "true" : "false");
            });
            col->append(*chk);
        } else if (is_color)
        {
            /* Color wheel only — never ask anyone to type "r g b a". */
            auto dialog = Gtk::ColorDialog::create();
            dialog->set_with_alpha(true);
            dialog->set_title(short_label);
            auto btn = Gtk::make_managed<Gtk::ColorDialogButton>(dialog);
            Gdk::RGBA rgba;
            if (!parse_wf_color(val, rgba))
            {
                rgba.set_rgba(0.1, 0.1, 0.1, 1.0);
            }
            btn->set_rgba(rgba);
            btn->set_hexpand(false);
            btn->set_tooltip_text("Click to open the color wheel");
            btn->property_rgba().signal_changed().connect([btn, commit] () {
                commit(format_wf_color(btn->get_rgba()));
            });
            col->append(*btn);
        } else if (!choices.empty() && (xml_type == "string" || xml_type.empty() ||
                   xml_type == "int"))
        {
            std::vector<Glib::ustring> labels;
            int selected = 0;
            for (size_t i = 0; i < choices.size(); ++i)
            {
                labels.push_back(choices[i].second);
                if (choices[i].first == val)
                {
                    selected = static_cast<int>(i);
                }
            }
            auto drop = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(labels));
            drop->set_selected(static_cast<guint>(selected));
            drop->set_hexpand(true);
            auto values = choices;
            drop->property_selected().signal_changed().connect([drop, values, commit] () {
                auto idx = drop->get_selected();
                if (idx != GTK_INVALID_LIST_POSITION && idx < values.size())
                {
                    commit(values[idx].first);
                }
            });
            col->append(*drop);
        } else if (is_dbl)
        {
            auto spin = Gtk::make_managed<Gtk::SpinButton>();
            double dv = 0;
            try { dv = std::stod(val); } catch (...) {}
            double minv = -1e12, maxv = 1e12;
            auto min_s = xml_child_text(onode, "min");
            auto max_s = xml_child_text(onode, "max");
            try { if (!min_s.empty()) minv = std::stod(min_s); } catch (...) {}
            try { if (!max_s.empty()) maxv = std::stod(max_s); } catch (...) {}
            spin->set_range(minv, maxv);
            spin->set_increments(0.05, 0.5);
            spin->set_digits(4);
            spin->set_value(dv);
            spin->set_hexpand(true);
            spin->signal_value_changed().connect([spin, commit] () {
                commit(std::to_string(spin->get_value()));
            });
            col->append(*spin);
        } else if (is_int)
        {
            auto spin = Gtk::make_managed<Gtk::SpinButton>();
            int iv = 0;
            try { iv = std::stoi(val); } catch (...) {}
            double minv = -1e9, maxv = 1e9;
            auto min_s = xml_child_text(onode, "min");
            auto max_s = xml_child_text(onode, "max");
            try { if (!min_s.empty()) minv = std::stod(min_s); } catch (...) {}
            try { if (!max_s.empty()) maxv = std::stod(max_s); } catch (...) {}
            spin->set_range(minv, maxv);
            spin->set_increments(1, 10);
            spin->set_value(iv);
            spin->set_hexpand(true);
            spin->signal_value_changed().connect([spin, commit] () {
                commit(std::to_string(static_cast<int>(spin->get_value())));
            });
            col->append(*spin);
        } else
        {
            auto ent = Gtk::make_managed<Gtk::Entry>();
            ent->set_text(val);
            ent->set_hexpand(true);
            auto setb = Gtk::make_managed<Gtk::Button>("Set");
            auto brow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            brow->append(*ent);
            brow->append(*setb);
            auto do_set = [ent, commit, opt] () {
                if (!commit(ent->get_text()))
                {
                    ent->set_text(opt->get_value_str());
                }
            };
            setb->signal_clicked().connect(do_set);
            ent->signal_activate().connect(do_set);
            col->append(*brow);
        }

        frame->set_child(*col);
        opt_box->append(*frame);
    }
}

} // namespace wf_settings
