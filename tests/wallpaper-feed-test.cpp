#include "wallpaper-util.hpp"

#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>

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

TEST(WallpaperUtil, ParsePicsumFeedValid)
{
    std::string valid_json = R"json(
    [
      {
        "id": "100",
        "author": "John Doe",
        "width": 2500,
        "height": 1667,
        "url": "https://unsplash.com/photos/...",
        "download_url": "https://picsum.photos/id/100/2500/1667"
      }
    ]
    )json";

    std::vector<OnlineImage> list;
    EXPECT_TRUE(parse_picsum_feed(valid_json, list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "picsum_100");
    EXPECT_EQ(list[0].author, "John Doe");
    EXPECT_EQ(list[0].download_url, "https://picsum.photos/id/100/2500/1667");
}

TEST(WallpaperUtil, ParsePicsumFeedMalformed)
{
    std::vector<OnlineImage> list;
    EXPECT_FALSE(parse_picsum_feed("{bad-json}", list));
    EXPECT_TRUE(parse_picsum_feed("[]", list)); // empty array is valid JSON
    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(parse_picsum_feed("404 Not Found", list));
}

TEST(WallpaperUtil, ParseBingFeedValid)
{
    std::string valid_json = R"json(
    {
      "images": [
        {
          "url": "/th?id=OHR.MountRainier_1920x1080.jpg",
          "urlbase": "/th?id=OHR.MountRainier",
          "copyright": "Mount Rainier (© Photographer)",
          "title": "Mount Rainier"
        }
      ]
    }
    )json";

    std::vector<OnlineImage> list;
    EXPECT_TRUE(parse_bing_feed(valid_json, list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "bing_0");
    EXPECT_EQ(list[0].author, "Mount Rainier (Mount Rainier (© Photographer))");
    EXPECT_EQ(list[0].download_url, "https://www.bing.com/th?id=OHR.MountRainier_1920x1080.jpg");
    EXPECT_EQ(list[0].thumb_url, "https://www.bing.com/th?id=OHR.MountRainier_320x180.jpg");
}

TEST(WallpaperUtil, ParseBingFeedMalformed)
{
    std::vector<OnlineImage> list;
    EXPECT_FALSE(parse_bing_feed("{bad-json}", list));
    EXPECT_TRUE(parse_bing_feed("{\"images\":[]}", list)); // empty images list is valid JSON structure
    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(parse_bing_feed("<html>Error</html>", list));
}

TEST(WallpaperUtil, ParseGithubTreeFeedValid)
{
    std::string valid_json = R"json(
    {
      "tree": [
        {
          "path": "minimal/sunset.png",
          "type": "blob"
        },
        {
          "path": "minimal/notes.txt",
          "type": "blob"
        },
        {
          "path": "subfolder",
          "type": "tree"
        }
      ]
    }
    )json";

    std::vector<OnlineImage> list;
    EXPECT_TRUE(parse_github_tree_feed(valid_json, "dharmx", list));
    ASSERT_EQ(list.size(), 1u); // only sunset.png matches blob + image extension
    EXPECT_EQ(list[0].id, "dharmx_0");
    EXPECT_EQ(list[0].author, "Sunset — dharmx (minimal)");
    EXPECT_EQ(list[0].download_url, "https://raw.githubusercontent.com/dharmx/walls/main/minimal/sunset.png");
}

TEST(WallpaperUtil, ParseWallhavenFeedValid)
{
    std::string valid_json = R"json(
    {
      "data": [
        {
          "id": "1qrorw",
          "path": "https://w.wallhaven.cc/full/1q/wallhaven-1qrorw.jpg",
          "category": "general",
          "resolution": "1920x1080",
          "thumbs": {
            "small": "https://th.wallhaven.cc/small/1q/1qrorw.jpg"
          }
        }
      ]
    }
    )json";

    std::vector<OnlineImage> list;
    EXPECT_TRUE(parse_wallhaven_feed(valid_json, list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "wallhaven_1qrorw");
    EXPECT_EQ(list[0].author, "Wallhaven variety (general 1920x1080)");
    EXPECT_EQ(list[0].download_url, "https://w.wallhaven.cc/full/1q/wallhaven-1qrorw.jpg");
    EXPECT_EQ(list[0].thumb_url, "https://th.wallhaven.cc/small/1q/1qrorw.jpg");
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

    // Create 3 files with different modification timestamps and sizes
    std::string f1 = temp + "/wallpaper_1.jpg";
    std::string f2 = temp + "/wallpaper_2.jpg";
    std::string meta = temp + "/metadata.json";

    write_temp_file(f1, std::string(300 * 1024, 'a')); // 300 KB
    write_temp_file(f2, std::string(300 * 1024, 'b')); // 300 KB
    write_temp_file(meta, "metadata");                // preserved

    std::error_code ec;
    // Set modification times: f1 is oldest, f2 is newer
    auto now = fs::last_write_time(meta, ec);
    fs::last_write_time(f1, now - std::chrono::hours(2), ec);
    fs::last_write_time(f2, now - std::chrono::hours(1), ec);

    // Call enforcer with 400 KB limit. It should delete f1 (the oldest file), leaving f2 and meta intact.
    enforce_cache_limit(temp, 400 * 1024);

    EXPECT_FALSE(fs::exists(f1, ec));
    EXPECT_TRUE(fs::exists(f2, ec));
    EXPECT_TRUE(fs::exists(meta, ec));

    fs::remove_all(temp, ec);
}

TEST(WallpaperUtil, StressParserRateLimits)
{
    // Generate massive malformed payload (10,000 bad array entries) to stress the parser limits
    std::ostringstream big_bad;
    big_bad << "[\n";
    for (int i = 0; i < 10000; ++i)
    {
        big_bad << "  {\"id\": " << i << ", \"author\": null, \"malformed\": true},\n";
    }
    big_bad << "  {\"id\": \"10001\", \"author\": \"Survivor\", \"download_url\": \"http://survivor.com/img.jpg\"}\n";
    big_bad << "]";

    std::vector<OnlineImage> list;
    // Parser must discard malformed fields and collect only valid entries, avoiding stack overflow
    EXPECT_TRUE(parse_unified_feed(big_bad.str(), list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "10001");
    EXPECT_EQ(list[0].author, "Survivor");
}
