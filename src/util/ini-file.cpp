#include "ini-file.hpp"

#include "user-config.hpp"

#include <cctype>
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

bool is_comment_or_empty(const std::string& t)
{
    return t.empty() || t[0] == '#' || t[0] == ';';
}

bool is_section_header(const std::string& t)
{
    return !t.empty() && t[0] == '[';
}

/**
 * wayfire.ini (and many INIs) allow backslash-continued values:
 *
 *   plugins = \
 *     alpha \
 *     animate
 *
 * Continuation lines have no '=', so a naive key replace leaves orphan lines
 * that poison the section on reload and can crash live Wayfire.
 *
 * A line is a continuation of the previous value if:
 *   - we are inside a section
 *   - the previous emitted "logical" line ended with unescaped '\'
 *   - this line is not empty/comment/section/key=
 */
bool line_ends_with_continuation(const std::string& raw)
{
    auto t = trim(raw);
    if (t.empty())
    {
        return false;
    }
    /* strip trailing whitespace already done; bare trailing '\' */
    return t.back() == '\\';
}

bool looks_like_assignment(const std::string& t)
{
    if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[')
    {
        return false;
    }
    auto eq = t.find('=');
    if (eq == std::string::npos || eq == 0)
    {
        return false;
    }
    /* key must be non-empty after trim of left side */
    return !trim(t.substr(0, eq)).empty();
}

std::vector<std::string> read_lines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (in && std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines)
{
    /* Parent dir must exist (first-run user prefs under ~/.config). */
    if (!ensure_parent_directories(path, nullptr))
    {
        return false;
    }
    /* Write via temp + rename so a concurrent wayfire inotify reload never
     * sees a half-written file (empty / truncated plugins=). */
    std::string tmp = path + ".tmp-wf-ini";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
        {
            return false;
        }
        for (const auto& l : lines)
        {
            out << l << "\n";
        }
        if (!out.flush())
        {
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        /* Fallback: direct write (e.g. cross-device) */
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            std::remove(tmp.c_str());
            return false;
        }
        for (const auto& l : lines)
        {
            out << l << "\n";
        }
        std::remove(tmp.c_str());
        return static_cast<bool>(out);
    }
    return true;
}

std::vector<std::string> format_value_lines(const std::string& key, const std::string& value)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : value)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == ',')
        {
            if (!cur.empty())
            {
                tokens.push_back(cur);
                cur.clear();
            }
        } else if (c != '\\')
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        tokens.push_back(cur);
    }

    /* Only expand to multiline for many tokens (typical core/plugins). */
    if (tokens.size() < 4)
    {
        return {key + " = " + value};
    }

    std::vector<std::string> out;
    out.push_back(key + " = \\");
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (i + 1 < tokens.size())
        {
            out.push_back("  " + tokens[i] + " \\");
        } else
        {
            out.push_back("  " + tokens[i]);
        }
    }
    return out;
}

} // namespace

std::string ini_get(const std::string& path, const std::string& section,
    const std::string& key)
{
    auto lines = read_lines(path);
    bool in = false;
    std::string last;
    const std::string hdr = "[" + section + "]";
    for (size_t i = 0; i < lines.size(); ++i)
    {
        auto t = trim(lines[i]);
        if (is_comment_or_empty(t))
        {
            continue;
        }
        if (is_section_header(t))
        {
            in = (t == hdr);
            continue;
        }
        if (!in)
        {
            continue;
        }
        if (!looks_like_assignment(t))
        {
            continue;
        }
        auto eq = t.find('=');
        auto k = trim(t.substr(0, eq));
        if (k != key)
        {
            continue;
        }
        std::string val = trim(t.substr(eq + 1));
        /*
         * Join backslash continuations.
         * wayfire style often starts with a lone '\':
         *   plugins = \
         *     alpha \
         *     animate
         * After stripping that first '\', val is empty — we must still keep
         * consuming continuation lines (do not require val to stay non-empty).
         */
        bool more = !val.empty() && val.back() == '\\';
        if (more)
        {
            val.pop_back();
            val = trim(val);
        }
        while (more)
        {
            if (i + 1 >= lines.size())
            {
                break;
            }
            ++i;
            auto cont = trim(lines[i]);
            if (is_comment_or_empty(cont) || is_section_header(cont) ||
                looks_like_assignment(cont))
            {
                --i; /* reprocess this line as a normal key/section */
                break;
            }
            more = !cont.empty() && cont.back() == '\\';
            if (more)
            {
                cont.pop_back();
                cont = trim(cont);
            }
            if (!cont.empty())
            {
                if (!val.empty())
                {
                    val += " ";
                }
                val += cont;
            }
        }
        last = val;
    }
    return last;
}

bool ini_set_many(const std::string& path, const std::string& section,
    const std::map<std::string, std::string>& kv)
{
    if (kv.empty())
    {
        return true;
    }
    auto lines = read_lines(path);
    const std::string hdr = "[" + section + "]";
    std::vector<std::string> out;
    bool in = false;
    bool have_sec = false;
    std::map<std::string, bool> written;
    for (const auto& [k, _] : kv)
    {
        written[k] = false;
    }

    bool skip_continuations = false;

    auto flush_unwritten = [&] () {
        for (const auto& [k, v] : kv)
        {
            if (!written[k])
            {
                auto fl = format_value_lines(k, v);
                out.insert(out.end(), fl.begin(), fl.end());
                written[k] = true;
            }
        }
    };

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto& l = lines[i];
        auto t = trim(l);

        if (is_section_header(t))
        {
            if (in)
            {
                flush_unwritten();
            }
            skip_continuations = false;
            in = (t == hdr);
            if (in)
            {
                have_sec = true;
            }
            out.push_back(l);
            continue;
        }

        if (in && skip_continuations)
        {
            /* Drop orphaned continuation lines of a replaced multiline value. */
            if (is_comment_or_empty(t) || is_section_header(t) || looks_like_assignment(t))
            {
                skip_continuations = false;
                /* fall through to process this line normally */
            } else
            {
                continue; /* discard continuation */
            }
        }

        if (in && looks_like_assignment(t))
        {
            auto eq = t.find('=');
            auto k = trim(t.substr(0, eq));
            if (kv.count(k))
            {
                if (!written[k])
                {
                    auto fl = format_value_lines(k, kv.at(k));
                    out.insert(out.end(), fl.begin(), fl.end());
                    written[k] = true;
                }
                /* Skip old value's backslash continuations */
                skip_continuations = line_ends_with_continuation(l);
                /* Also skip if next lines are continuations even without trailing \
                 * on the key line (malformed) — handled by skip when not assignment */
                if (!skip_continuations)
                {
                    /* Peek: if next line is indented non-assignment, treat as cont */
                    if (i + 1 < lines.size())
                    {
                        auto nt = trim(lines[i + 1]);
                        if (!is_comment_or_empty(nt) && !is_section_header(nt) &&
                            !looks_like_assignment(nt))
                        {
                            skip_continuations = true;
                        }
                    }
                }
                continue;
            }
        }

        if (in && !looks_like_assignment(t) && !is_comment_or_empty(t) &&
            !is_section_header(t))
        {
            /* Standalone continuation of a non-managed key — keep it only if we
             * did not just replace a managed key (skip_continuations handles that). */
        }

        out.push_back(l);
    }

    if (in)
    {
        flush_unwritten();
    }
    if (!have_sec)
    {
        if (!out.empty() && !out.back().empty())
        {
            out.push_back("");
        }
        out.push_back(hdr);
        for (const auto& [k, v] : kv)
        {
            auto fl = format_value_lines(k, v);
            out.insert(out.end(), fl.begin(), fl.end());
        }
    }
    return write_lines(path, out);
}

bool ini_set(const std::string& path, const std::string& section,
    const std::string& key, const std::string& value)
{
    return ini_set_many(path, section, {{key, value}});
}

bool ini_get_bool(const std::string& path, const std::string& section,
    const std::string& key, bool default_val)
{
    auto v = ini_get(path, section, key);
    if (v.empty())
    {
        return default_val;
    }
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

int ini_get_int(const std::string& path, const std::string& section,
    const std::string& key, int default_val)
{
    auto v = ini_get(path, section, key);
    if (v.empty())
    {
        return default_val;
    }
    try
    {
        return std::stoi(v);
    } catch (...)
    {
        return default_val;
    }
}

} // namespace wf_shell
