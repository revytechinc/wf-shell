# Wallpaper Feeds & Discovery Registry

This file documents the wallpaper feeds, APIs, schemas, caching parameters, and local directories used by the **wf-settings** application to dynamically discover, search, and apply desktop wallpapers.

---

## 1. Local Assets Directory
Local wallpapers shipped directly by the package are installed in:
* **Source path:** `data/backgrounds/`
* **Target path:** `${PREFIX}/share/wf-shell/backgrounds/`

### Curated Dharmx Wallpapers (Stalenhag Collection)
We bundle a subset of high-resolution digital art from [dharmx/walls](https://github.com/dharmx/walls) by Simon Stålenhag:
1. `dharmx-bird-statue.jpg` — A bird resting on a monumental statue.
2. `dharmx-man-robot.jpg` — A man meeting a large robot in an empty parking lot.
3. `dharmx-snow-machines.jpg` — Massive mechanical machines traversing a winter highway.

---

## 2. Dynamic Online Feed API Registry

The desktop page queries multiple feeds concurrently in a detached background thread to populate the discoverable online wallpapers catalog:

### Feed 1: Bing Daily Image Archive
* **URL:** `https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=8&mkt=en-US`
* **Format:** JSON object containing `images` array.
* **Metadata Fields mapped:**
  * `url`: Appended to `https://www.bing.com` for direct high-res download.
  * `urlbase`: Appended with `_320x180.jpg` for compressed thumbnail fetch.
  * `copyright` & `title`: Mapped as author/search metadata.

### Feed 2: Picsum Photos API
* **URL:** `https://picsum.photos/v2/list?limit=60`
* **Format:** JSON array of objects.
* **Metadata Fields mapped:**
  * `id`: Unique image key (prefixed with `picsum_` internally).
  * `author`: Photographer's name.
  * Direct thumbnail resolution requested via: `https://picsum.photos/id/{id}/200/120`.
  * Direct wallpaper resolution requested via: `https://picsum.photos/id/{id}/1920/1080`.

### Feed 3: Dharmx Walls GitHub Tree API
* **URL:** `https://api.github.com/repos/dharmx/walls/git/trees/main?recursive=1`
* **Headers required:** `User-Agent: wf-settings`
* **Format:** JSON object containing a recursive `tree` of repository entries.
* **Mapping Logic:**
  * Filters for `blob` type nodes ending with `.png`, `.jpg`, or `.jpeg`.
  * Direct raw download URL: `https://raw.githubusercontent.com/dharmx/walls/main/{path}`.
  * Categories and titles parsed directly from folder names and filenames (e.g. `stalenhag/underpass.jpg` ➔ title `"Underpass"`, category `"stalenhag"`).

### Feed 4: OneDark Wallpapers GitHub Tree API
* **URL:** `https://api.github.com/repos/Narmis-E/onedark-wallpapers/git/trees/main?recursive=1`
* **Headers required:** `User-Agent: wf-settings`
* **Format:** JSON object containing a recursive `tree` of repository entries.
* **Mapping Logic:**
  * Filters for `blob` type nodes ending with `.png`, `.jpg`, or `.jpeg`.
  * Direct raw download URL: `https://raw.githubusercontent.com/Narmis-E/onedark-wallpapers/main/{path}`.
  * Categories and titles parsed directly from folder/file paths (e.g. `Abstract/onedark_abstract.png` ➔ title `"Onedark Abstract"`, category `"Abstract"`).

### Feed 5: Wallhaven Variety Collection API
* **URL:** `https://wallhaven.cc/api/v1/collections/lewdpatriot/935888?page=1`
* **Format:** JSON object containing `data` array.
* **Mapping Logic:**
  * Maps `id` (prefixed with `wallhaven_`).
  * Direct raw download URL: `path` string.
  * Thumbnail URL: `thumbs.small` string.
  * Metadata generated as `"Wallhaven variety ({category} {resolution})"`.

---

## 3. Caching & Cache Boundary Rules

> [!IMPORTANT]
> The wallpaper feed client uses a fail-soft, fault-tolerant boundary design. All operations are non-blocking to prevent UI lockups during slow network conditions or rate-limiting.

### Unified Cache Files
* **Metadata Cache:** `~/.cache/wf-shell/wallpapers/metadata.json`
* **Thumbnails Cache:** `~/.cache/wf-shell/wallpapers/thumb_{id}.jpg`
* **Full Wallpapers Cache:** `~/.cache/wf-shell/wallpapers/wallpaper_{id}.jpg`

### Unified Metadata Schema
Both local and online images are combined into a single unified JSON array in `metadata.json` for fast offline loading:
```json
[
  {
    "id": "bing_0",
    "author": "Mount Rainier (© John Doe)",
    "download_url": "https://www.bing.com/th?id=OHR.MountRainier...",
    "thumb_url": "https://www.bing.com/th?id=OHR.MountRainier_320x180.jpg"
  },
  {
    "id": "picsum_10",
    "author": "Alejandro Escamilla",
    "download_url": "https://unsplash.com/photos/...",
    "thumb_url": ""
  }
]
```

### Storage Limit Policy
* **Limit:** Maximum cache directory size is restricted to **2 GB**.
* **Clean-up Rule:** Once the cache surpasses 2 GB, the oldest accessed wallpaper files (sorted by modification time) are automatically pruned by the `enforce_cache_limit()` routine.

---

## 4. Troubleshooting & Developer Guidelines

* **Adding a New Feed:**
  1. Append the fetch logic inside `DesktopPage::fetch_online_feed()`'s background thread block.
  2. Parse the result into `OnlineImage` structs.
  3. Map unique thumbnail/download URLs.
  4. Ensure any network fetches have a timeout parameter (`-m 4` or `-T 4`).
* **Debugging Local Cache:**
  To completely clear the feed cache and trigger a fresh reload, run:
  ```bash
  rm -rf ~/.cache/wf-shell/wallpapers/
  ```
