// On-disk caches: per-image measurements, finished layouts and layout seeds.
//
// Everything lives in a private per-user directory under the temp folder. Each input
// folder gets a base path; layouts and seeds are stored beside it, keyed by signatures
// of every setting that can change the result.
#pragma once

#include "commands/spratlayout/common.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sprat::layout {

struct ImageCacheEntry {
    bool trim_transparent = false;
    uintmax_t file_size = 0;
    long long mtime_ticks = 0;
    int w = 0;
    int h = 0;
    int trim_left = 0;
    int trim_top = 0;
    int trim_right = 0;
    int trim_bottom = 0;
    long long cached_at_unix = 0;
    uint64_t content_hash = 0;     // FNV-1a hash of visible pixel region (0 = not computed)
    uint64_t perceptual_hash = 0;  // dHash of visible pixel region
    bool has_perceptual_hash = false;  // a dHash of 0 is valid (flat or ramp sprites)
    bool has_slice = false;
    int slice_left = 0, slice_top = 0, slice_right = 0, slice_bottom = 0;
};

using ImageCache = std::unordered_map<std::string, ImageCacheEntry>;

std::string image_cache_key(const std::string& path, bool trim_transparent);

// Every setting that can change a layout. The seed and incremental signatures use a subset.
struct LayoutCacheKey {
    std::string profile_name;
    Mode mode = Mode::FAST;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    int max_width_limit = 0;
    int max_height_limit = 0;
    int padding = 0;
    int extrude = 0;
    double scale = 1.0;
    bool trim_transparent = false;
    bool allow_rotate = false;
    bool preserve_source_order = false;
    bool multipack = false;
    bool rotate_ccw = false;
    std::string order_key;
    std::string deduplicate_mode;
    int dedup_threshold = 0;
    int dedup_tolerance = 0;
};

// Identifies the requested sprite order so layouts sorted differently never share a cache entry.
std::string build_order_key(bool has_frame_sort_override, FrameSort frame_sort, StableMetric stable_metric);
std::string build_layout_signature(const LayoutCacheKey& key, const std::vector<ImageSource>& sources);
std::string build_layout_seed_signature(const LayoutCacheKey& key, const std::vector<ImageSource>& sources);
std::string build_incremental_seed_signature(const LayoutCacheKey& key, const fs::path& working_dir);

long long now_unix_seconds();

// Base cache path for an input folder; empty when no safe cache directory exists.
// An empty base disables caching: the paths built from it are empty as well.
fs::path build_cache_path(const fs::path& folder);
fs::path build_output_cache_path(const fs::path& base_cache_path, const std::string& layout_signature);
fs::path build_seed_cache_path(const fs::path& base_cache_path, const std::string& seed_signature);
fs::path build_incremental_seed_cache_path(const fs::path& base_cache_path, const std::string& signature);

// False when the file is missing or older than the cache lifetime.
bool is_cache_file_fresh(const fs::path& path);

bool load_image_cache(const fs::path& cache_path, ImageCache& out);
bool save_image_cache(const fs::path& cache_path, const ImageCache& entries);
void prune_stale_cache_entries(ImageCache& entries, long long now_unix);

bool load_output_cache(const fs::path& cache_path, const std::string& expected_signature, std::string& output);
bool save_output_cache(const fs::path& cache_path, const std::string& signature, const std::string& output);

bool load_layout_seed_cache(const fs::path& cache_path, const std::string& expected_signature, LayoutSeedCache& out);
bool save_layout_seed_cache(const fs::path& cache_path, const LayoutSeedCache& seed);
LayoutSeedCache make_layout_seed(const std::string& signature, int padding,
                                 int atlas_width, int atlas_height,
                                 const std::vector<Sprite>& sprites);

// Removes legacy, stale and excess cache files of every input folder.
void prune_all_caches();
// Removes stale and excess layout and seed files of one input folder.
void prune_cache_family(const fs::path& base_cache_path);

} // namespace sprat::layout
