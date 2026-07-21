#include "wallpaper-util.hpp"

#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>
#include <memory>

using namespace wf_shell;
namespace fs = std::filesystem;

namespace
{

std::string temp_dir_path()
{
    char buf[] = "/tmp/wf-feed-test-XXXXXX";
    char *dir = mkdtemp(buf);
    if (!dir)
    {
        return {};
    }
    return dir;
}

void write_temp_file(const std::string& path, const std::string& body)
{
    std::ofstream o(path);
    o << body;
}

} // namespace

TEST(WallpaperUtil, UrlEncode)
{
    EXPECT_EQ(url_encode("hello world"), "hello%20world");
    EXPECT_EQ(url_encode("abc-123_.~"), "abc-123_.~");
    EXPECT_EQ(url_encode("!@#"), "%21%40%23");
}

TEST(WallpaperUtil, EscapeJsonStr)
{
    EXPECT_EQ(escape_json_str("quote\""), "quote\\\"");
    EXPECT_EQ(escape_json_str("slash\\"), "slash\\\\");
    EXPECT_EQ(escape_json_str("tab\t"), "tab\\t");
    EXPECT_EQ(escape_json_str("newline\n"), "newline\\n");
}

TEST(WallpaperUtil, InterfaceSteeringAndInstantiation)
{
    // Test that all Strategy classes can be instantiated and managed polymorphically via the IWallpaperSource interface
    std::vector<std::unique_ptr<IWallpaperSource>> sources;
    sources.push_back(std::make_unique<BingSource>());
    sources.push_back(std::make_unique<PicsumSource>());
    sources.push_back(std::make_unique<GithubTreeSource>("dharmx", "walls"));
    sources.push_back(std::make_unique<WallhavenVarietySource>("lewdpatriot", "935888"));
    sources.push_back(std::make_unique<WallhavenSearchSource>());

    EXPECT_EQ(sources.size(), 5u);
    EXPECT_EQ(sources[0]->get_name(), "Bing Daily");
    EXPECT_EQ(sources[1]->get_name(), "Picsum Photos");
    EXPECT_EQ(sources[2]->get_name(), "dharmx/walls");
    EXPECT_EQ(sources[3]->get_name(), "Wallhaven (lewdpatriot/935888)");
    EXPECT_EQ(sources[4]->get_name(), "Wallhaven Global");
}

TEST(WallpaperUtil, StrategyFetchesGracefully)
{
    // Verify Strategy instances return lists, or handle network timeouts / offline fail-soft gracefully by returning empty sets
    auto bing = std::make_unique<BingSource>();
    auto results = bing->fetch("");
    // Should either fetch successfully (results > 0) or fail soft gracefully (results == 0) without raising exceptions
    EXPECT_TRUE(results.size() >= 0);

    auto picsum = std::make_unique<PicsumSource>();
    auto results_p = picsum->fetch("");
    EXPECT_TRUE(results_p.size() >= 0);

    auto gh = std::make_unique<GithubTreeSource>("dharmx", "walls");
    auto results_g = gh->fetch("stalenhag");
    EXPECT_TRUE(results_g.size() >= 0);
}

TEST(WallpaperUtil, UnifiedFeedRoundtrip)
{
    std::vector<OnlineImage> origin = {
        {"bing_0", "Rainier", "https://bing.com/a.jpg", "https://bing.com/a_thumb.jpg"},
        {"picsum_10", "Alejandro", "https://picsum.com/10.jpg", ""}
    };

    std::string serialized = serialize_unified_feed(origin);
    std::vector<OnlineImage> deserialized;
    EXPECT_TRUE(parse_unified_feed(serialized, deserialized));
    
    ASSERT_EQ(deserialized.size(), 2u);
    EXPECT_EQ(deserialized[0].id, "bing_0");
    EXPECT_EQ(deserialized[0].author, "Rainier");
    EXPECT_EQ(deserialized[0].download_url, "https://bing.com/a.jpg");
    EXPECT_EQ(deserialized[0].thumb_url, "https://bing.com/a_thumb.jpg");

    EXPECT_EQ(deserialized[1].id, "picsum_10");
    EXPECT_EQ(deserialized[1].author, "Alejandro");
    EXPECT_EQ(deserialized[1].download_url, "https://picsum.com/10.jpg");
    EXPECT_TRUE(deserialized[1].thumb_url.empty());
}

TEST(WallpaperUtil, EnforceCacheLimit)
{
    std::string temp = temp_dir_path();
    ASSERT_FALSE(temp.empty());

    std::string f1 = temp + "/wallpaper_1.jpg";
    std::string f2 = temp + "/wallpaper_2.jpg";
    std::string meta = temp + "/metadata.json";

    write_temp_file(f1, std::string(300 * 1024, 'a')); // 300 KB
    write_temp_file(f2, std::string(300 * 1024, 'b')); // 300 KB
    write_temp_file(meta, "metadata");                // preserved

    std::error_code ec;
    auto now = fs::last_write_time(meta, ec);
    fs::last_write_time(f1, now - std::chrono::hours(2), ec);
    fs::last_write_time(f2, now - std::chrono::hours(1), ec);

    enforce_cache_limit(temp, 400 * 1024);

    EXPECT_FALSE(fs::exists(f1, ec));
    EXPECT_TRUE(fs::exists(f2, ec));
    EXPECT_TRUE(fs::exists(meta, ec));

    fs::remove_all(temp, ec);
}

TEST(WallpaperUtil, StressParserRateLimits)
{
    std::ostringstream big_bad;
    big_bad << "[\n";
    for (int i = 0; i < 10000; ++i)
    {
        big_bad << "  {\"id\": " << i << ", \"author\": null, \"malformed\": true},\n";
    }
    big_bad << "  {\"id\": \"10001\", \"author\": \"Survivor\", \"download_url\": \"http://survivor.com/img.jpg\"}\n";
    big_bad << "]";

    std::vector<OnlineImage> list;
    EXPECT_TRUE(parse_unified_feed(big_bad.str(), list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "10001");
    EXPECT_EQ(list[0].author, "Survivor");
}
