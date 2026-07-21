#pragma once

#include <string>
#include <vector>

namespace wf_shell
{

struct OnlineImage
{
    std::string id;
    std::string author;
    std::string download_url;
    std::string thumb_url;
};

std::string url_encode(const std::string& value);
std::string escape_json_str(const std::string& s);
void enforce_cache_limit(const std::string& cache_dir, uintmax_t limit_bytes);

bool parse_picsum_feed(const std::string& json_text, std::vector<OnlineImage>& out);
bool parse_bing_feed(const std::string& json_text, std::vector<OnlineImage>& out);
bool parse_github_tree_feed(const std::string& json_text, const std::string& repo_name, std::vector<OnlineImage>& out);
bool parse_wallhaven_feed(const std::string& json_text, std::vector<OnlineImage>& out);
bool parse_unified_feed(const std::string& json_text, std::vector<OnlineImage>& out);
std::string serialize_unified_feed(const std::vector<OnlineImage>& list);

} // namespace wf_shell
