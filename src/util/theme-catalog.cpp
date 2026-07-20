#include "theme-catalog.hpp"

#include "theme-defaults.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace wf_shell
{
namespace
{

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
    {
        ++i;
    }
    return s.substr(i);
}

std::string title_case_id(const std::string& id)
{
    std::string out = id;
    bool cap = true;
    for (char& c : out)
    {
        if (c == '-' || c == '_')
        {
            c   = ' ';
            cap = true;
            continue;
        }
        if (cap && std::isalpha(static_cast<unsigned char>(c)))
        {
            c   = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap = false;
        } else
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

void parse_header(const std::string& path, ThemePack& te)
{
    std::ifstream in(path);
    if (!in)
    {
        return;
    }
    std::string line;
    int n = 0;
    while (n++ < 40 && std::getline(in, line))
    {
        auto t = trim(line);
        if (t.rfind("/*", 0) == 0)
        {
            continue;
        }
        if (t.rfind("* Name:", 0) == 0 || t.rfind("* name:", 0) == 0)
        {
            auto p = t.find(':');
            if (p != std::string::npos)
            {
                te.name = trim(t.substr(p + 1));
            }
        }
        if (t.rfind("* Era:", 0) == 0 || t.rfind("* era:", 0) == 0)
        {
            auto p = t.find(':');
            if (p != std::string::npos)
            {
                te.era = trim(t.substr(p + 1));
                for (char& c : te.era)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
        }
        if (t.find("*/") != std::string::npos)
        {
            break;
        }
    }
}

} // namespace

std::map<std::string, ThemePack> discover_theme_packs(
    const std::string& resource_themes_dir,
    const std::string& user_themes_dir)
{
    std::map<std::string, ThemePack> themes;
    themes["default"] = ThemePack{"default", "Default Theme", "", ""};

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
            ThemePack te;
            te.id   = p.path().stem().string();
            te.name = title_case_id(te.id);
            te.path = p.path().string();
            parse_header(te.path, te);
            if (te.name.empty())
            {
                te.name = title_case_id(te.id);
            }
            if (te.id == "default")
            {
                continue;
            }
            if (user || themes.find(te.id) == themes.end())
            {
                themes[te.id] = te;
            }
        }
    };

    scan_dir(resource_themes_dir, false);
    if (!user_themes_dir.empty())
    {
        scan_dir(user_themes_dir, true);
    }
    return themes;
}

std::vector<ThemePack> theme_packs_ui_order(const std::map<std::string, ThemePack>& packs)
{
    std::vector<ThemePack> modern, retro, other, out;
    for (const auto& kv : packs)
    {
        if (kv.first == "default")
        {
            continue;
        }
        if (kv.second.era == "modern")
        {
            modern.push_back(kv.second);
        } else if (kv.second.era == "retro")
        {
            retro.push_back(kv.second);
        } else
        {
            other.push_back(kv.second);
        }
    }
    auto by_name = [] (const ThemePack& a, const ThemePack& b) {
        return a.name < b.name;
    };
    std::sort(modern.begin(), modern.end(), by_name);
    std::sort(retro.begin(), retro.end(), by_name);
    std::sort(other.begin(), other.end(), by_name);

    if (auto it = packs.find("default"); it != packs.end())
    {
        out.push_back(it->second);
    }
    out.insert(out.end(), modern.begin(), modern.end());
    out.insert(out.end(), other.begin(), other.end());
    out.insert(out.end(), retro.begin(), retro.end());
    return out;
}

std::string get_ini_css_path(const std::string& ini_path)
{
    std::ifstream in(ini_path);
    if (!in)
    {
        return {};
    }
    std::string line, last;
    bool in_panel = false;
    while (std::getline(in, line))
    {
        auto t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';')
        {
            continue;
        }
        if (t[0] == '[')
        {
            in_panel = (t == "[panel]" || t.rfind("[panel]", 0) == 0);
            continue;
        }
        if (in_panel && t.rfind("css_path", 0) == 0)
        {
            auto eq = t.find('=');
            if (eq != std::string::npos)
            {
                last = trim(t.substr(eq + 1));
            }
        }
    }
    return last;
}

bool update_ini_css_path(const std::string& ini_path, const std::string& path)
{
    std::ifstream in(ini_path);
    std::vector<std::string> raw;
    std::string line;
    if (in)
    {
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            raw.push_back(line);
        }
    }

    std::vector<std::string> lines;
    bool in_panel = false;
    bool found    = false;
    bool have_panel = false;
    for (const auto& l : raw)
    {
        auto t = trim(l);
        if (!t.empty() && t[0] == '[')
        {
            in_panel = (t == "[panel]");
            if (in_panel)
            {
                have_panel = true;
            }
        }
        if (t.rfind("css_path", 0) == 0)
        {
            if (in_panel && !found)
            {
                lines.push_back("css_path = " + path);
                found = true;
            }
            /* drop duplicate css_path lines */
            continue;
        }
        lines.push_back(l);
    }
    if (!found)
    {
        if (!have_panel)
        {
            lines.push_back("[panel]");
            lines.push_back("css_path = " + path);
        } else
        {
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (trim(lines[i]) == "[panel]")
                {
                    lines.insert(lines.begin() + static_cast<long>(i) + 1,
                        "css_path = " + path);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                lines.push_back("css_path = " + path);
            }
        }
    }

    std::ofstream out(ini_path, std::ios::trunc);
    if (!out)
    {
        return false;
    }
    for (const auto& l : lines)
    {
        out << l << "\n";
    }
    return true;
}

bool apply_theme_pack(const std::string& theme_id,
    const std::string& ini_path,
    const std::string& resource_themes_dir,
    const std::string& user_themes_dir,
    std::string *error)
{
    auto packs = discover_theme_packs(resource_themes_dir, user_themes_dir);
    std::string css;
    if (theme_id.empty() || theme_id == "default")
    {
        css = {};
    } else
    {
        auto it = packs.find(theme_id);
        if (it == packs.end())
        {
            if (error)
            {
                *error = "unknown theme: " + theme_id;
            }
            return false;
        }
        css = it->second.path;
    }
    if (!update_ini_css_path(ini_path, css))
    {
        if (error)
        {
            *error = "failed to write " + ini_path;
        }
        return false;
    }
    return true;
}

} // namespace wf_shell
