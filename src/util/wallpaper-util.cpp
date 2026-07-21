#include "wallpaper-util.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <wayfire/nonstd/json.hpp>

namespace wf_shell
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

void enforce_cache_limit(const std::string& cache_dir, uintmax_t limit_bytes)
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

    if (total_size > limit_bytes)
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
                    if (total_size <= limit_bytes) break;
                }
            }
        }
    }
}

bool parse_picsum_feed(const std::string& json_text, std::vector<OnlineImage>& out)
{
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err || !root.is_array())
    {
        return false;
    }
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
            out.push_back(img);
        }
    }
    return true;
}

bool parse_bing_feed(const std::string& json_text, std::vector<OnlineImage>& out)
{
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err || !root.is_object() || !root.has_member("images") || !root["images"].is_array())
    {
        return false;
    }
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
            out.push_back(img);
        }
    }
    return true;
}

bool parse_github_tree_feed(const std::string& json_text, const std::string& repo_name, std::vector<OnlineImage>& out)
{
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err || !root.is_object() || !root.has_member("tree") || !root["tree"].is_array())
    {
        return false;
    }
    auto tree = root["tree"];
    int count = 0;
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
                std::string category = repo_name;
                size_t slash = path.find('/');
                if (slash != std::string::npos)
                {
                    category = repo_name + " (" + path.substr(0, slash) + ")";
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
                img.id = repo_name + "_" + std::to_string(count++);
                img.author = filename + " — " + category;
                img.download_url = "https://raw.githubusercontent.com/" + repo_name + "/walls/main/" + path;
                if (repo_name == "onedark-wallpapers")
                {
                    img.download_url = "https://raw.githubusercontent.com/Narmis-E/onedark-wallpapers/main/" + path;
                }
                img.thumb_url = img.download_url;
                out.push_back(img);
            }
        }
    }
    return true;
}

bool parse_wallhaven_feed(const std::string& json_text, std::vector<OnlineImage>& out)
{
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err || !root.is_object() || !root.has_member("data") || !root["data"].is_array())
    {
        return false;
    }
    auto data = root["data"];
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
            out.push_back(img);
        }
    }
    return true;
}

bool parse_unified_feed(const std::string& json_text, std::vector<OnlineImage>& out)
{
    wf::json_t root;
    auto err = wf::json_t::parse_string(json_text, root);
    if (err || !root.is_array())
    {
        return false;
    }
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
            out.push_back(img);
        }
    }
    return true;
}

std::string serialize_unified_feed(const std::vector<OnlineImage>& list)
{
    std::ostringstream o;
    o << "[\n";
    for (size_t i = 0; i < list.size(); ++i)
    {
        o << "  {\n";
        o << "    \"id\": \"" << escape_json_str(list[i].id) << "\",\n";
        o << "    \"author\": \"" << escape_json_str(list[i].author) << "\",\n";
        o << "    \"download_url\": \"" << escape_json_str(list[i].download_url) << "\",\n";
        o << "    \"thumb_url\": \"" << escape_json_str(list[i].thumb_url) << "\"\n";
        o << "  }" << (i + 1 < list.size() ? "," : "") << "\n";
    }
    o << "]";
    return o.str();
}

} // namespace wf_shell
