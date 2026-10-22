#include "commands/spratlayout/sprites.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <stb_image.h>

#include "core/cli_parse.h"
#include "core/fnv1a.h"
#include "core/i18n.h"
#include "core/parallel.h"
#include "core/perceptual_dedup.h"

namespace sprat::layout {

using sprat::core::tr;
using sprat::core::to_quoted;

namespace {

// Sprites may share an atlas region only if their nine-slice insets match:
// slices are per-region metadata that an alias cannot override.
bool same_slice(const Sprite& a, const Sprite& b) {
    return a.has_slice == b.has_slice
        && (!a.has_slice
            || (a.slice_left == b.slice_left && a.slice_top == b.slice_top
                && a.slice_right == b.slice_right && a.slice_bottom == b.slice_bottom));
}

// The alias records its own trim only when it differs from the canonical's,
// so equal-offset duplicates keep the short two-path alias line.
sprat::core::Alias make_layout_alias(const Sprite& duplicate, const Sprite& canonical) {
    sprat::core::Alias alias;
    alias.path = duplicate.path;
    alias.canonical_path = canonical.path;
    alias.trim_left = duplicate.trim_left;
    alias.trim_top = duplicate.trim_top;
    alias.trim_right = duplicate.trim_right;
    alias.trim_bottom = duplicate.trim_bottom;
    alias.has_trim = duplicate.trim_left != canonical.trim_left
        || duplicate.trim_top != canonical.trim_top
        || duplicate.trim_right != canonical.trim_right
        || duplicate.trim_bottom != canonical.trim_bottom;
    return alias;
}

bool compute_trim_bounds(const unsigned char* rgba,
                         int w,
                         int h,
                         int& min_x,
                         int& min_y,
                         int& max_x,
                         int& max_y) {
    min_x = 0;
    min_y = 0;
    max_x = -1;
    max_y = -1;
    if (w <= 0 || h <= 0 || rgba == nullptr) {
        return false;
    }

    // Validate that the image dimensions are reasonable
    if (w > k_max_image_dimension || h > k_max_image_dimension) {
        return false;
    }

    // Use direct pointer arithmetic for alpha channel access.
    // Bounds are already validated above (w > 0, h > 0, w/h <= k_max_image_dimension).
    const size_t stride = static_cast<size_t>(w) * 4;

    int top_hit_x = -1;
    for (int y = 0; y < h; ++y) {
        const unsigned char* row_alpha = rgba + static_cast<size_t>(y) * stride + 3;
        for (int x = 0; x < w; ++x) {
            if (row_alpha[static_cast<size_t>(x) * 4] != 0) {
                min_y = y;
                top_hit_x = x;
                break;
            }
        }
        if (top_hit_x >= 0) {
            break;
        }
    }
    if (top_hit_x < 0) {
        return false;
    }

    int bottom_hit_x = -1;
    for (int y = h - 1; y >= min_y; --y) {
        const unsigned char* row_alpha = rgba + static_cast<size_t>(y) * stride + 3;
        for (int x = w - 1; x >= 0; --x) {
            if (row_alpha[static_cast<size_t>(x) * 4] != 0) {
                max_y = y;
                bottom_hit_x = x;
                break;
            }
        }
        if (bottom_hit_x >= 0) {
            break;
        }
    }

    const int left_search_end = std::min(top_hit_x, bottom_hit_x);
    min_x = left_search_end;
    for (int x = 0; x <= left_search_end; ++x) {
        bool found = false;
        const unsigned char* col_alpha = rgba + static_cast<size_t>(x) * 4 + 3;
        for (int y = min_y; y <= max_y; ++y) {
            if (col_alpha[static_cast<size_t>(y) * stride] != 0) {
                min_x = x;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }

    const int right_search_start = std::max(top_hit_x, bottom_hit_x);
    max_x = right_search_start;
    for (int x = w - 1; x >= right_search_start; --x) {
        bool found = false;
        const unsigned char* col_alpha = rgba + static_cast<size_t>(x) * 4 + 3;
        for (int y = min_y; y <= max_y; ++y) {
            if (col_alpha[static_cast<size_t>(y) * stride] != 0) {
                max_x = x;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }

    return max_x >= min_x && max_y >= min_y;
}

// Loads the RGBA pixels that would be packed for `s`: the (trim_left, trim_top, w, h)
// region of its source file. Returns nullopt if the file can't be read or the region
// doesn't fit (e.g. the file changed since it was scanned).
std::optional<std::vector<unsigned char>> load_sprite_region(const Sprite& s) {
    int file_w = 0;
    int file_h = 0;
    int channels = 0;
    unsigned char* px = stbi_load(s.path.c_str(), &file_w, &file_h, &channels, 4);
    if (px == nullptr) {
        return std::nullopt;
    }
    std::optional<std::vector<unsigned char>> region;
    if (s.w > 0 && s.h > 0 && s.trim_left >= 0 && s.trim_top >= 0 &&
        s.trim_left <= file_w - s.w && s.trim_top <= file_h - s.h) {
        const size_t row_bytes = static_cast<size_t>(s.w) * 4;
        region.emplace(row_bytes * static_cast<size_t>(s.h));
        for (int y = 0; y < s.h; ++y) {
            const unsigned char* src = px +
                (static_cast<size_t>(s.trim_top + y) * static_cast<size_t>(file_w) +
                 static_cast<size_t>(s.trim_left)) * 4;
            std::memcpy(region->data() + static_cast<size_t>(y) * row_bytes, src, row_bytes);
        }
    }
    stbi_image_free(px);
    return region;
}


// Every fully transparent sprite gets the same hashes, so they deduplicate together.
constexpr uint64_t k_transparent_sprite_hash = 0xFFFFFFFFFFFFFFFFULL;

// Parses Android nine-patch markers from the 1px opaque border of a full image.
// rgba  : RGBA pixel data for the full fw x fh image (including the 1px border).
// On success, fills sl/st/sr/sb with content-space insets and returns true.
bool parse_nine_patch(const unsigned char* rgba, int fw, int fh,
                      int& sl, int& st, int& sr, int& sb) {
    if (fw < 3 || fh < 3) return false;
    auto is_black = [&](int col, int row) -> bool {
        const unsigned char* p =
            rgba + (static_cast<size_t>(row) * static_cast<size_t>(fw) + static_cast<size_t>(col)) * 4;
        return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255;
    };
    int first_col = -1, last_col = -1;
    for (int c = 1; c < fw - 1; ++c) {
        if (is_black(c, 0)) {
            if (first_col < 0) first_col = c;
            last_col = c;
        }
    }
    int first_row = -1, last_row = -1;
    for (int r = 1; r < fh - 1; ++r) {
        if (is_black(0, r)) {
            if (first_row < 0) first_row = r;
            last_row = r;
        }
    }
    if (first_col < 0 || first_row < 0) return false;
    sl = first_col - 1;
    sr = (fw - 2) - last_col;
    st = first_row - 1;
    sb = (fh - 2) - last_row;
    return true;
}

struct Region {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Copies a region of an RGBA image that is `stride_w` pixels wide.
std::vector<unsigned char> copy_region(const unsigned char* rgba, int stride_w, const Region& r) {
    const size_t row_bytes = static_cast<size_t>(r.w) * 4;
    std::vector<unsigned char> out(row_bytes * static_cast<size_t>(r.h));
    for (int ry = 0; ry < r.h; ++ry) {
        const unsigned char* src = rgba +
            (static_cast<size_t>(r.y + ry) * static_cast<size_t>(stride_w) + static_cast<size_t>(r.x)) * 4;
        std::memcpy(out.data() + static_cast<size_t>(ry) * row_bytes, src, row_bytes);
    }
    return out;
}

// FNV-1a of the region's pixels, row by row.
uint64_t hash_region(const unsigned char* rgba, int stride_w, const Region& r) {
    uint64_t hash = sprat::core::k_fnv1a_offset_basis;
    for (int ry = 0; ry < r.h; ++ry) {
        const unsigned char* row = rgba +
            (static_cast<size_t>(r.y + ry) * static_cast<size_t>(stride_w) + static_cast<size_t>(r.x)) * 4;
        hash = sprat::core::fnv1a_update(hash, row, static_cast<size_t>(r.w) * 4);
    }
    return hash;
}

struct SpriteLoadResult {
    bool ok = false;
    bool from_cache = false;
    std::string fail_reason;
    Sprite sprite;
    ImageCacheEntry new_entry;
};

// A cache entry is reusable if the file is unchanged and it has the hashes the
// deduplication mode needs.
bool sprite_from_cache(const ImageCacheEntry& cached, const ImageSource& source,
                       const SpriteLoadOptions& options, Sprite& out) {
    if (cached.trim_transparent != options.trim_transparent ||
        cached.file_size != source.meta.file_size ||
        cached.mtime_ticks != source.meta.mtime_ticks) {
        return false;
    }
    if (options.deduplicate_mode == "exact" && cached.content_hash == 0) {
        return false;
    }
    if (options.deduplicate_mode == "perceptual" && !cached.has_perceptual_hash) {
        return false;
    }
    out = Sprite{};
    out.path = source.path;
    out.w = cached.w;
    out.h = cached.h;
    out.trim_left = cached.trim_left;
    out.trim_top = cached.trim_top;
    out.trim_right = cached.trim_right;
    out.trim_bottom = cached.trim_bottom;
    out.has_slice = cached.has_slice;
    out.slice_left = cached.slice_left;
    out.slice_top = cached.slice_top;
    out.slice_right = cached.slice_right;
    out.slice_bottom = cached.slice_bottom;
    return true;
}

// Measures one image: its packed size, trim offsets, nine-patch slice and the
// hashes used for deduplication.
//
// The packed region is always (trim_left, trim_top, w, h) in the source file. A
// nine-patch's 1px marker border is excluded by counting it as trim.
SpriteLoadResult measure_source(const ImageSource& source, const SpriteLoadOptions& options) {
    SpriteLoadResult result;
    Sprite& sprite = result.sprite;
    sprite.path = source.path;
    const std::string& path = source.path;
    const bool dedup = options.deduplicate_mode != "none";
    const bool is_nine_patch = sprat::core::is_nine_patch_path(path);
    uint64_t content_hash = 0;
    uint64_t perceptual_hash = 0;

    int w = 0;
    int h = 0;
    int channels = 0;
    if (!options.trim_transparent && !dedup && !is_nine_patch) {
        // Only the size is needed.
        if (stbi_info(path.c_str(), &w, &h, &channels) == 0) {
            result.fail_reason = stbi_failure_reason();
            return result;
        }
        sprite.w = w;
        sprite.h = h;
    } else {
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (px == nullptr) {
            result.fail_reason = stbi_failure_reason();
            return result;
        }

        const int border = (is_nine_patch && w >= 3 && h >= 3) ? 1 : 0;
        if (border > 0) {
            int sl = 0, st = 0, sr = 0, sb = 0;
            if (parse_nine_patch(px, w, h, sl, st, sr, sb)) {
                sprite.has_slice = true;
                sprite.slice_left = sl;
                sprite.slice_top = st;
                sprite.slice_right = sr;
                sprite.slice_bottom = sb;
            }
        }
        const int content_w = w - 2 * border;
        const int content_h = h - 2 * border;
        std::vector<unsigned char> inner;
        const unsigned char* content = px;
        // Only trimming and hashing read the pixels; a plain nine-patch needs just its size.
        if (border > 0 && (options.trim_transparent || dedup)) {
            inner = copy_region(px, w, {border, border, content_w, content_h});
            content = inner.data();
        }

        // The packed region, in content coordinates.
        Region visible{0, 0, content_w, content_h};
        bool fully_transparent = false;
        if (options.trim_transparent) {
            int min_x = 0;
            int min_y = 0;
            int max_x = -1;
            int max_y = -1;
            if (compute_trim_bounds(content, content_w, content_h, min_x, min_y, max_x, max_y)) {
                visible = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
                sprite.trim_left = border + min_x;
                sprite.trim_top = border + min_y;
                sprite.trim_right = border + (content_w - 1) - max_x;
                sprite.trim_bottom = border + (content_h - 1) - max_y;
            } else {
                // Fully transparent image: keep a 1x1 transparent region.
                fully_transparent = true;
                visible = {0, 0, 1, 1};
                sprite.trim_left = border;
                sprite.trim_top = border;
                sprite.trim_right = border + std::max(0, content_w - 1);
                sprite.trim_bottom = border + std::max(0, content_h - 1);
            }
        } else {
            sprite.trim_left = sprite.trim_top = sprite.trim_right = sprite.trim_bottom = border;
        }
        sprite.w = visible.w;
        sprite.h = visible.h;

        if (dedup) {
            if (fully_transparent) {
                content_hash = k_transparent_sprite_hash;
                perceptual_hash = k_transparent_sprite_hash;
            } else {
                content_hash = hash_region(content, content_w, visible);
                if (visible.x == 0 && visible.y == 0 && visible.w == content_w && visible.h == content_h) {
                    perceptual_hash = sprat::core::compute_dhash(content, visible.w, visible.h);
                } else {
                    const std::vector<unsigned char> region = copy_region(content, content_w, visible);
                    perceptual_hash = sprat::core::compute_dhash(region.data(), visible.w, visible.h);
                }
            }
        }
        stbi_image_free(px);
    }

    result.ok = true;
    result.new_entry = {
        .trim_transparent = options.trim_transparent,
        .file_size = source.meta.file_size,
        .mtime_ticks = source.meta.mtime_ticks,
        .w = sprite.w,
        .h = sprite.h,
        .trim_left = sprite.trim_left,
        .trim_top = sprite.trim_top,
        .trim_right = sprite.trim_right,
        .trim_bottom = sprite.trim_bottom,
        .cached_at_unix = 0,
        .content_hash = content_hash,
        .perceptual_hash = perceptual_hash,
        .has_perceptual_hash = dedup,
        .has_slice = sprite.has_slice,
        .slice_left = sprite.slice_left,
        .slice_top = sprite.slice_top,
        .slice_right = sprite.slice_right,
        .slice_bottom = sprite.slice_bottom,
    };
    return result;
}

void deduplicate_exact(std::vector<Sprite>& sprites, bool trim_transparent,
                       const ImageCache& cache_entries,
                       std::vector<sprat::core::Alias>& out_aliases) {
    // O(N) hash-map dedup keyed by (content_hash, w, h, slice insets)
    struct DedupKey {
        uint64_t hash;
        int w;
        int h;
        std::array<int, 4> slice;  // all -1 when the sprite has no slice
        bool operator==(const DedupKey& o) const {
            return hash == o.hash && w == o.w && h == o.h && slice == o.slice;
        }
    };
    struct DedupKeyHash {
        size_t operator()(const DedupKey& k) const {
            size_t h = std::hash<uint64_t>{}(k.hash);
            h ^= std::hash<int>{}(k.w) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>{}(k.h) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            for (int v : k.slice) {
                h ^= std::hash<int>{}(v) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            }
            return h;
        }
    };
    // Maps to the canonical sprite's index in `deduped`.
    std::unordered_map<DedupKey, size_t, DedupKeyHash> canonical_map;
    std::vector<Sprite> deduped;
    deduped.reserve(sprites.size());
    for (const auto& s : sprites) {
        const auto it = cache_entries.find(image_cache_key(s.path, trim_transparent));
        const uint64_t h = (it != cache_entries.end()) ? it->second.content_hash : 0;
        if (h == 0) {
            deduped.push_back(s);
            continue;
        }
        const std::array<int, 4> slice = s.has_slice
            ? std::array<int, 4>{s.slice_left, s.slice_top, s.slice_right, s.slice_bottom}
            : std::array<int, 4>{-1, -1, -1, -1};
        DedupKey key{h, s.w, s.h, slice};
        auto [ins_it, inserted] = canonical_map.emplace(key, deduped.size());
        if (inserted) {
            deduped.push_back(s);
        } else {
            out_aliases.push_back(make_layout_alias(s, deduped[ins_it->second]));
        }
    }
    sprites = std::move(deduped);
}

void deduplicate_perceptual(std::vector<Sprite>& sprites, bool trim_transparent,
                            int dedup_threshold, int dedup_tolerance,
                            const ImageCache& cache_entries,
                            std::vector<sprat::core::Alias>& out_aliases) {
    // The dHash only nominates candidates; pixels decide. Each sprite's packed region
    // is (trim_left, trim_top, w, h) in its source file, for trimmed and nine-patch
    // sprites alike, so we compare exactly what would end up in the atlas.
    std::vector<sprat::core::DedupCandidate> candidates(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        const Sprite& s = sprites[i];
        candidates[i].w = s.w;
        candidates[i].h = s.h;
        const auto it = cache_entries.find(image_cache_key(s.path, trim_transparent));
        if (it != cache_entries.end() && it->second.has_perceptual_hash) {
            candidates[i].hash = it->second.perceptual_hash;
            candidates[i].has_hash = true;
        }
    }
    std::unordered_map<size_t, std::optional<std::vector<unsigned char>>> region_cache;
    const auto region_of = [&](size_t i) -> const std::optional<std::vector<unsigned char>>& {
        auto [it, inserted] = region_cache.try_emplace(i);
        if (inserted) {
            it->second = load_sprite_region(sprites[i]);
        }
        return it->second;
    };
    const std::vector<size_t> canonical_of = sprat::core::group_near_duplicates(
        candidates, dedup_threshold, [&](size_t canonical, size_t candidate) {
            const auto& a = region_of(canonical);
            const auto& b = region_of(candidate);
            return a && b && same_slice(sprites[canonical], sprites[candidate])
                && sprat::core::rgba_within_tolerance(
                a->data(), b->data(),
                static_cast<size_t>(sprites[candidate].w) * static_cast<size_t>(sprites[candidate].h),
                dedup_tolerance);
        });
    std::vector<Sprite> deduped;
    deduped.reserve(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        if (canonical_of[i] == i) {
            deduped.push_back(sprites[i]);
        } else {
            out_aliases.push_back(make_layout_alias(sprites[i], sprites[canonical_of[i]]));
        }
    }
    sprites = std::move(deduped);
}
} // namespace

std::vector<Sprite> load_sprites(const std::vector<ImageSource>& sources,
                                 const SpriteLoadOptions& options,
                                 ImageCache& cache_entries,
                                 long long now_unix) {
    const size_t source_count = sources.size();
    std::vector<SpriteLoadResult> load_results(source_count);

    // Workers only read cache_entries; it is updated in the serial pass below.
    auto process_source = [&](size_t i) {
        const ImageSource& source = sources[i];
        SpriteLoadResult& result = load_results[i];
        const auto cache_it = cache_entries.find(image_cache_key(source.path, options.trim_transparent));
        if (cache_it != cache_entries.end() &&
            sprite_from_cache(cache_it->second, source, options, result.sprite)) {
            result.ok = true;
            result.from_cache = true;
            return;
        }
        result = measure_source(source, options);
        result.new_entry.cached_at_unix = now_unix;
    };

    const unsigned int load_worker_count = std::min<unsigned int>(
        resolve_worker_count(options.thread_limit), static_cast<unsigned int>(source_count));
    if (load_worker_count <= 1 || source_count <= 1) {
        for (size_t i = 0; i < source_count; ++i) process_source(i);
    } else {
        sprat::core::run_workers(load_worker_count, [&](unsigned int wi) {
            const size_t begin = (source_count * wi) / load_worker_count;
            const size_t end   = (source_count * (wi + 1)) / load_worker_count;
            for (size_t i = begin; i < end; ++i) process_source(i);
        });
    }

    // Serial collection pass: merge results in source order.
    std::vector<Sprite> sprites;
    sprites.reserve(source_count);
    for (size_t i = 0; i < source_count; ++i) {
        const SpriteLoadResult& r = load_results[i];
        if (!r.ok) {
            std::cerr << tr("Warning: Failed to load sprite ")
                      << to_quoted(sources[i].path)
                      << tr(" (Reason: ") << r.fail_reason << tr(")\n");
            continue;
        }
        sprites.push_back(r.sprite);
        const std::string key = image_cache_key(sources[i].path, options.trim_transparent);
        if (r.from_cache) {
            cache_entries[key].cached_at_unix = now_unix;
        } else {
            cache_entries[key] = r.new_entry;
        }
    }
    return sprites;
}

void deduplicate_sprites(std::vector<Sprite>& sprites,
                         const std::string& deduplicate_mode,
                         bool trim_transparent,
                         int dedup_threshold,
                         int dedup_tolerance,
                         const ImageCache& cache_entries,
                         std::vector<sprat::core::Alias>& out_aliases) {
    if (deduplicate_mode == "exact") {
        deduplicate_exact(sprites, trim_transparent, cache_entries, out_aliases);
    } else if (deduplicate_mode == "perceptual") {
        deduplicate_perceptual(sprites, trim_transparent, dedup_threshold, dedup_tolerance,
                               cache_entries, out_aliases);
    }
}

bool scale_dimension(int input, double scale, int& output) {
    if (input <= 0 || scale <= 0.0) {
        return false;
    }
    long double scaled = static_cast<long double>(input) * static_cast<long double>(scale);
    if (scaled > static_cast<long double>(std::numeric_limits<int>::max())) {
        return false;
    }
    int rounded = static_cast<int>(std::lround(scaled));
    if (rounded <= 0) {
        rounded = 1;
    }
    output = rounded;
    return true;
}

} // namespace sprat::layout
