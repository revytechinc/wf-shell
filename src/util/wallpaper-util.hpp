#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

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

// Strategy Interface for Wallpaper Sources
class IWallpaperSource
{
public:
    virtual ~IWallpaperSource() = default;
    virtual std::string get_name() const = 0;
    
    // Executes fetch operation; if query is provided, performs filtering or searching
    virtual std::vector<OnlineImage> fetch(const std::string& query) = 0;
};

// Concrete source implementations
class BingSource : public IWallpaperSource
{
public:
    std::string get_name() const override { return "Bing Daily"; }
    std::vector<OnlineImage> fetch(const std::string& query) override;
};

class PicsumSource : public IWallpaperSource
{
public:
    std::string get_name() const override { return "Picsum Photos"; }
    std::vector<OnlineImage> fetch(const std::string& query) override;
};

class GithubTreeSource : public IWallpaperSource
{
private:
    std::string repo_owner;
    std::string repo_name;
    std::string branch;
public:
    GithubTreeSource(std::string owner, std::string name, std::string br = "main")
        : repo_owner(owner), repo_name(name), branch(br) {}
    std::string get_name() const override { return repo_owner + "/" + repo_name; }
    std::vector<OnlineImage> fetch(const std::string& query) override;
};

class WallhavenVarietySource : public IWallpaperSource
{
private:
    std::string username;
    std::string collection_id;
public:
    WallhavenVarietySource(std::string user, std::string col_id)
        : username(user), collection_id(col_id) {}
    std::string get_name() const override { return "Wallhaven (" + username + "/" + collection_id + ")"; }
    std::vector<OnlineImage> fetch(const std::string& query) override;
};

class WallhavenSearchSource : public IWallpaperSource
{
public:
    std::string get_name() const override { return "Wallhaven Global"; }
    std::vector<OnlineImage> fetch(const std::string& query) override;
};

// Unified helper function to fetch from cached metadata
bool parse_unified_feed(const std::string& json_text, std::vector<OnlineImage>& out);
std::string serialize_unified_feed(const std::vector<OnlineImage>& list);

} // namespace wf_shell
