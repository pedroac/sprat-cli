#include "commands/spratlayout/cache.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_set>

#include "core/fnv1a.h"

namespace sprat::layout {

namespace {

constexpr int k_image_cache_format_version = 5;
constexpr int k_output_cache_format_version = 5;
constexpr int k_seed_cache_format_version = 4;
constexpr size_t k_max_cache_entries = 10000;
constexpr long long k_max_cache_age_seconds_limit = 31536000; // 1 year
constexpr int k_floating_point_precision = 17;
constexpr long long k_cache_max_age_seconds = 3600;
constexpr size_t k_cache_max_layout_files = 16;
constexpr size_t k_cache_max_seed_files = 8;

// A cache file path is usable only when it lives inside a cache directory.
bool is_cache_path_usable(const fs::path& cache_path) {
    return !cache_path.empty() && !cache_path.parent_path().empty();
}

// Unique per writer so concurrent runs never write into the same temporary file.
fs::path make_cache_tmp_path(const fs::path& cache_path) {
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const size_t thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::array<char, 24> suffix{};
    std::snprintf(suffix.data(), suffix.size(), ".%016llx.tmp",
                  static_cast<unsigned long long>(thread_hash) ^ stamp);
    fs::path tmp = cache_path;
    tmp += suffix.data();
    return tmp;
}

fs::path default_temp_dir() {
    std::error_code ec;
    fs::path path = fs::temp_directory_path(ec);
    if (!ec && !path.empty()) {
        return path;
    }

    const char* tmp = std::getenv("TMP");
    if (tmp != nullptr && *tmp != '\0') {
        return fs::path(tmp);
    }
    const char* temp = std::getenv("TEMP");
    if (temp != nullptr && *temp != '\0') {
        return fs::path(temp);
    }
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir != nullptr && *tmpdir != '\0') {
        return fs::path(tmpdir);
    }

#ifdef _WIN32
    return fs::path(".");
#else
    return fs::path("/tmp");
#endif
}

// The temp directory is shared between users on POSIX systems, so the cache lives in a
// per-user directory that must be private: otherwise another user could plant cache
// entries (wrong layouts) or symlinks that redirect our writes.
#ifndef _WIN32
bool is_private_cache_dir(const fs::path& dir) {
    struct stat st {};
    if (lstat(dir.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}
#endif

// Returns an empty path when no safe cache directory is available; caching is then skipped.
fs::path cache_root_dir() {
#ifdef _WIN32
    fs::path root = default_temp_dir() / "sprat";
    std::error_code ec;
    fs::create_directories(root, ec);
    return ec ? fs::path() : root;
#else
    fs::path root = default_temp_dir() / ("sprat-" + std::to_string(geteuid()));
    if (mkdir(root.c_str(), S_IRWXU) != 0 && errno != EEXIST) {
        return {};
    }
    return is_private_cache_dir(root) ? root : fs::path();
#endif
}

std::string to_hex_size_t(size_t value) {
    std::array<char, 20> buf{};
    int n = std::snprintf(buf.data(), buf.size(), "%zx", value);
    return std::string(buf.data(), n > 0 ? static_cast<size_t>(n) : 0);
}

bool is_file_older_than_seconds(const fs::path& path, long long max_age_seconds) {
    if (max_age_seconds < 0 || max_age_seconds > k_max_cache_age_seconds_limit) { // 1 year limit
        return true;
    }
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        return true;
    }
    fs::file_time_type file_time = fs::last_write_time(path, ec);
    if (ec) {
        return true;
    }
    fs::file_time_type now = fs::file_time_type::clock::now();
    if (file_time > now) {
        return true; // future mtime (clock skew); treat as stale
    }
    long long age = std::chrono::duration_cast<std::chrono::seconds>(now - file_time).count();
    return age > max_age_seconds;
}

// Appends a "\npath|file_size|mtime" line per source, sorted unless the source order matters.
void append_source_signature(std::string& sig,
                             bool preserve_source_order,
                             const std::vector<ImageSource>& sources) {
    std::vector<std::string> parts;
    parts.reserve(sources.size());
    for (const auto& source : sources) {
        std::string line;
        line += source.path;
        line += '|';
        line += std::to_string(source.meta.file_size);
        line += '|';
        line += std::to_string(source.meta.mtime_ticks);
        parts.push_back(std::move(line));
    }
    if (!preserve_source_order) {
        std::ranges::sort(parts);
    }
    for (const std::string& part : parts) {
        sig += '\n';
        sig += part;
    }
}

// Joins signature fields with '|'.
class SignatureBuilder {
public:
    SignatureBuilder& add(const std::string& value) {
        if (fields_ > 0) {
            sig_ += '|';
        }
        ++fields_;
        sig_ += value;
        return *this;
    }
    SignatureBuilder& add(int value) { return add(std::to_string(value)); }
    SignatureBuilder& add_flag(bool value) { return add(value ? "1" : "0"); }
    SignatureBuilder& add_scale(double scale) {
        std::array<char, 32> scale_buf{};
        std::snprintf(scale_buf.data(), scale_buf.size(), "%.*g", k_floating_point_precision, scale);
        return add(std::string(scale_buf.data()));
    }
    // The fields every signature starts with.
    SignatureBuilder& add_profile_and_limits(const LayoutCacheKey& key) {
        return add(key.profile_name)
            .add(static_cast<int>(key.mode))
            .add(static_cast<int>(key.optimize_target))
            .add(key.max_width_limit)
            .add(key.max_height_limit);
    }
    std::string& text() { return sig_; }
    std::string hash() const { return to_hex_size_t(std::hash<std::string>{}(sig_)); }

private:
    std::string sig_;
    size_t fields_ = 0;
};

// An empty base (caching disabled) gives an empty path, which every load and save rejects.
fs::path cache_file_path(const fs::path& base_cache_path, const char* kind, const std::string& signature) {
    if (base_cache_path.empty()) {
        return {};
    }
    return base_cache_path.string() + kind + signature;
}

// Writes through a uniquely named temporary file and renames it into place, so readers
// never see a partially written cache file.
bool write_cache_file(const fs::path& cache_path,
                      std::ios::openmode mode,
                      const std::function<void(std::ostream&)>& write) {
    if (!is_cache_path_usable(cache_path)) {
        return false;
    }
    const fs::path tmp = make_cache_tmp_path(cache_path);

    std::ofstream out(tmp, mode | std::ios::trunc);
    if (!out) {
        return false;
    }
    write(out);
    out.close();
    if (!out) {
        return false;
    }

    std::error_code ec;
    fs::rename(tmp, cache_path, ec);
    if (ec) {
        fs::remove(cache_path, ec);
        ec.clear();
        fs::rename(tmp, cache_path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

void prune_cache_family_group(const fs::path& base_cache_path,
                              const std::string& group_suffix,
                              long long max_age_seconds,
                              size_t max_files_to_keep) {
    if (max_files_to_keep == 0) {
        return;
    }

    const fs::path parent = base_cache_path.parent_path();
    if (parent.empty()) {
        return;
    }

    const std::string prefix = base_cache_path.filename().string() + group_suffix;
    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    const fs::file_time_type now = fs::file_time_type::clock::now();
    struct CacheFileInfo {
        fs::path path;
        fs::file_time_type mtime;
    };
    std::vector<CacheFileInfo> keep_candidates;

    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (!name.starts_with(prefix)) {
            continue;
        }

        if (name.ends_with(".tmp")) {
            fs::remove(entry.path(), ec);
            ec.clear();
            continue;
        }

        fs::file_time_type mtime = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }

        bool remove_for_age = false;
        if (mtime <= now) {
            const long long age = std::chrono::duration_cast<std::chrono::seconds>(now - mtime).count();
            if (age > max_age_seconds) {
                remove_for_age = true;
            }
        }
        if (remove_for_age) {
            fs::remove(entry.path(), ec);
            ec.clear();
            continue;
        }

        keep_candidates.push_back({.path=entry.path(), .mtime=mtime});
    }

    if (keep_candidates.size() <= max_files_to_keep) {
        return;
    }

    std::ranges::sort(keep_candidates, [](const CacheFileInfo& a, const CacheFileInfo& b) {
        return a.mtime > b.mtime;
    });

    for (size_t i = max_files_to_keep; i < keep_candidates.size(); ++i) {
        fs::remove(keep_candidates[i].path, ec);
        ec.clear();
    }
}

void remove_legacy_top_level_cache_files() {
    const fs::path parent = default_temp_dir();
    const fs::path active_root = cache_root_dir();
    if (parent == active_root) {
        return;
    }

    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("spratlayout_")) {
            continue;
        }
        if (name.find(".cache") == std::string::npos) {
            continue;
        }
        fs::remove(entry.path(), ec);
        ec.clear();
    }
}

void prune_family(const fs::path& base_cache_path, long long max_age_seconds) {
    prune_cache_family_group(base_cache_path, ".layout.", max_age_seconds, k_cache_max_layout_files);
    prune_cache_family_group(base_cache_path, ".seed.", max_age_seconds, k_cache_max_seed_files);
}

} // namespace

std::string image_cache_key(const std::string& path, bool trim_transparent) {
    return path + (trim_transparent ? "|1" : "|0");
}

long long now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool is_cache_file_fresh(const fs::path& path) {
    return !is_file_older_than_seconds(path, k_cache_max_age_seconds);
}

void prune_stale_cache_entries(ImageCache& entries, long long now_unix) {
    for (auto it = entries.begin(); it != entries.end();) {
        const long long cached_at = it->second.cached_at_unix;
        if (cached_at <= 0 || cached_at > now_unix || (now_unix - cached_at) > k_cache_max_age_seconds) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

bool load_image_cache(const fs::path& cache_path, ImageCache& out) {
    out.clear();
    std::ifstream in(cache_path);
    if (!in) {
        return false;
    }

    std::string header_tag;
    int version = 0;
    if (!(in >> header_tag >> version)) {
        return false;
    }
    if (header_tag != "spratlayout_cache" || (version < 1 || version > k_image_cache_format_version)) {
        return false;
    }

    std::string path;
    int trim_flag = 0;
    ImageCacheEntry entry;
    while (true) {
        entry = ImageCacheEntry{};
        if (!(in >> std::quoted(path)
                 >> trim_flag
                 >> entry.file_size
                 >> entry.mtime_ticks
                 >> entry.w
                 >> entry.h
                 >> entry.trim_left
                 >> entry.trim_top
                 >> entry.trim_right
                 >> entry.trim_bottom)) {
            break;
        }
        if (version >= 2) {
            if (!(in >> entry.cached_at_unix)) {
                break;
            }
        }
        if (version >= 3) {
            if (!(in >> entry.content_hash >> entry.perceptual_hash)) {
                break;
            }
        }
        if (version >= 4) {
            int slice_flag = 0;
            if (!(in >> slice_flag >> entry.slice_left >> entry.slice_top >> entry.slice_right >> entry.slice_bottom)) {
                break;
            }
            entry.has_slice = slice_flag != 0;
        }
        if (version >= 5) {
            int phash_flag = 0;
            if (!(in >> phash_flag)) {
                break;
            }
            entry.has_perceptual_hash = phash_flag != 0;
        } else {
            // Older caches used 0 as "not computed"; recompute those.
            entry.has_perceptual_hash = entry.perceptual_hash != 0;
        }
        if (entry.w <= 0 || entry.h <= 0 || entry.w > k_max_source_image_dimension || entry.h > k_max_source_image_dimension) {
            continue;
        }
        entry.trim_transparent = trim_flag != 0;
        out[image_cache_key(path, entry.trim_transparent)] = entry;
        if (out.size() >= k_max_cache_entries) {
            break;
        }
    }

    return true;
}

bool save_image_cache(const fs::path& cache_path, const ImageCache& entries) {
    // Collect valid entries; if over the limit, keep only the most recently used.
    using KV = std::pair<const std::string*, const ImageCacheEntry*>;
    std::vector<KV> valid;
    valid.reserve(entries.size());
    for (const auto& kv : entries) {
        const ImageCacheEntry& e = kv.second;
        if (e.w > 0 && e.h > 0 && e.w <= k_max_source_image_dimension && e.h <= k_max_source_image_dimension) {
            valid.push_back({&kv.first, &e});
        }
    }
    if (valid.size() > k_max_cache_entries) {
        std::ranges::sort(valid, [](const KV& a, const KV& b) {
            return a.second->cached_at_unix > b.second->cached_at_unix; // newest first
        });
        valid.resize(k_max_cache_entries);
    }

    return write_cache_file(cache_path, std::ios::out, [&](std::ostream& out) {
        out << "spratlayout_cache " << k_image_cache_format_version << "\n";
        for (const auto& [key_ptr, e_ptr] : valid) {
            // Strip the "|0"/"|1" trim suffix of the key; the flag is its own field.
            std::string path = *key_ptr;
            if (path.size() > 2 &&
                path[path.size() - 2] == '|' &&
                (path.back() == '0' || path.back() == '1')) {
                path = path.substr(0, path.size() - 2);
            }
            const ImageCacheEntry& e = *e_ptr;
            out << std::quoted(path) << " "
                << (e.trim_transparent ? 1 : 0) << " "
                << e.file_size << " "
                << e.mtime_ticks << " "
                << e.w << " "
                << e.h << " "
                << e.trim_left << " "
                << e.trim_top << " "
                << e.trim_right << " "
                << e.trim_bottom << " "
                << e.cached_at_unix << " "
                << e.content_hash << " "
                << e.perceptual_hash << " "
                << (e.has_slice ? 1 : 0) << " "
                << e.slice_left << " "
                << e.slice_top << " "
                << e.slice_right << " "
                << e.slice_bottom << " "
                << (e.has_perceptual_hash ? 1 : 0) << "\n";
        }
    });
}

fs::path build_cache_path(const fs::path& folder) {
    std::error_code ec;
    fs::path normalized = fs::absolute(folder, ec);
    std::string folder_key = (!ec ? normalized.lexically_normal().string() : folder.string());
    // Every cache of a folder hangs off this path. Keying it by the program version too keeps a
    // new release from serving image measurements and layouts computed by an older one.
    folder_key += '|';
    folder_key += SPRAT_VERSION;
    uint64_t hash = sprat::core::fnv1a_hash(
        reinterpret_cast<const unsigned char*>(folder_key.data()), folder_key.size());

    std::array<char, 40> buf{};
    std::snprintf(buf.data(), buf.size(), "spratlayout_%016llx.cache",
                  static_cast<unsigned long long>(hash));
    const fs::path root = cache_root_dir();
    if (root.empty()) {
        return {};
    }
    return root / buf.data();
}

fs::path build_output_cache_path(const fs::path& base_cache_path,
                                 const std::string& layout_signature) {
    return cache_file_path(base_cache_path, ".layout.", layout_signature);
}

fs::path build_seed_cache_path(const fs::path& base_cache_path,
                               const std::string& seed_signature) {
    return cache_file_path(base_cache_path, ".seed.", seed_signature);
}

fs::path build_incremental_seed_cache_path(const fs::path& base_cache_path,
                                           const std::string& signature) {
    return cache_file_path(base_cache_path, ".iseed.", signature);
}

std::string build_order_key(bool has_frame_sort_override, FrameSort frame_sort, StableMetric stable_metric) {
    if (!has_frame_sort_override) {
        return "default";
    }
    return std::to_string(static_cast<int>(frame_sort)) + ':' + std::to_string(static_cast<int>(stable_metric));
}

std::string build_layout_signature(const LayoutCacheKey& key, const std::vector<ImageSource>& sources) {
    SignatureBuilder sig;
    sig.add_profile_and_limits(key)
        .add(key.padding)
        .add(key.extrude)
        .add_scale(key.scale)
        .add_flag(key.trim_transparent)
        .add_flag(key.allow_rotate)
        .add_flag(key.preserve_source_order)
        .add_flag(key.multipack)
        .add(key.rotate_ccw ? "ccw" : "cw")
        .add(key.order_key)
        .add(key.deduplicate_mode)
        .add(key.dedup_threshold)
        .add(key.dedup_tolerance);
    append_source_signature(sig.text(), key.preserve_source_order, sources);
    return sig.hash();
}

// Seeds hold positions only, so padding (checked when applying a seed), the rotation
// direction and deduplication (applied before packing) are not part of the signature.
std::string build_layout_seed_signature(const LayoutCacheKey& key, const std::vector<ImageSource>& sources) {
    SignatureBuilder sig;
    sig.add_profile_and_limits(key)
        .add(key.extrude)
        .add_scale(key.scale)
        .add_flag(key.trim_transparent)
        .add_flag(key.allow_rotate)
        .add_flag(key.preserve_source_order)
        .add_flag(key.multipack)
        .add(key.order_key);
    append_source_signature(sig.text(), key.preserve_source_order, sources);
    return sig.hash();
}

// Keyed by the folder instead of its files, so the seed survives added and changed sprites.
std::string build_incremental_seed_signature(const LayoutCacheKey& key, const fs::path& working_dir) {
    SignatureBuilder sig;
    sig.add("iseed")
        .add_profile_and_limits(key)
        .add(key.extrude)
        .add_scale(key.scale)
        .add_flag(key.trim_transparent)
        .add_flag(key.allow_rotate)
        .add(key.order_key)
        .add(working_dir.string());
    return sig.hash();
}

bool load_output_cache(const fs::path& cache_path,
                       const std::string& expected_signature,
                       std::string& output) {
    std::ifstream in(cache_path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string expected_header = "spratlayout_output_cache " + std::to_string(k_output_cache_format_version);
    std::string header;
    if (!std::getline(in, header) || header != expected_header) {
        return false;
    }

    std::string signature;
    if (!std::getline(in, signature) || signature != expected_signature) {
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return false;
    }
    output = buffer.str();
    return true;
}

bool save_output_cache(const fs::path& cache_path,
                       const std::string& signature,
                       const std::string& output) {
    return write_cache_file(cache_path, std::ios::out | std::ios::binary, [&](std::ostream& out) {
        out << "spratlayout_output_cache " << k_output_cache_format_version << "\n";
        out << signature << "\n";
        out << output;
    });
}

bool load_layout_seed_cache(const fs::path& cache_path,
                            const std::string& expected_signature,
                            LayoutSeedCache& out) {
    out = LayoutSeedCache{};
    std::ifstream in(cache_path);
    if (!in) {
        return false;
    }

    std::string header_tag;
    int version = 0;
    if (!(in >> header_tag >> version)) {
        return false;
    }
    if (header_tag != "spratlayout_seed_cache" ||
        (version != 2 && version != k_seed_cache_format_version)) {
        return false;
    }

    if (!(in >> out.signature) || out.signature != expected_signature) {
        return false;
    }

    size_t count = 0;
    if (!(in >> out.padding >> out.atlas_width >> out.atlas_height >> count)) {
        return false;
    }
    if (count == 0 || out.atlas_width <= 0 || out.atlas_height <= 0) {
        return false;
    }

    out.entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        LayoutSeedEntry entry;
        if (!(in >> std::quoted(entry.path)
                 >> entry.x >> entry.y
                 >> entry.w >> entry.h
                 >> entry.trim_left >> entry.trim_top
                 >> entry.trim_right >> entry.trim_bottom)) {
            return false;
        }
        if (version >= 3) {
            int rotated_flag = 0;
            if (!(in >> rotated_flag)) {
                return false;
            }
            entry.rotated = rotated_flag != 0;
        }
        out.entries.push_back(std::move(entry));
    }

    return true;
}

bool save_layout_seed_cache(const fs::path& cache_path,
                            const LayoutSeedCache& seed) {
    if (seed.signature.empty() || seed.entries.empty() ||
        seed.atlas_width <= 0 || seed.atlas_height <= 0) {
        return false;
    }

    return write_cache_file(cache_path, std::ios::out, [&](std::ostream& out) {
        out << "spratlayout_seed_cache " << k_seed_cache_format_version << "\n";
        out << seed.signature << "\n";
        out << seed.padding << " "
            << seed.atlas_width << " "
            << seed.atlas_height << " "
            << seed.entries.size() << "\n";
        for (const auto& entry : seed.entries) {
            out << std::quoted(entry.path) << " "
                << entry.x << " "
                << entry.y << " "
                << entry.w << " "
                << entry.h << " "
                << entry.trim_left << " "
                << entry.trim_top << " "
                << entry.trim_right << " "
                << entry.trim_bottom << " "
                << (entry.rotated ? 1 : 0) << "\n";
        }
    });
}

LayoutSeedCache make_layout_seed(const std::string& signature, int padding,
                                 int atlas_width, int atlas_height,
                                 const std::vector<Sprite>& sprites) {
    LayoutSeedCache seed;
    seed.signature = signature;
    seed.padding = padding;
    seed.atlas_width = atlas_width;
    seed.atlas_height = atlas_height;
    seed.entries.reserve(sprites.size());
    for (const auto& s : sprites) {
        LayoutSeedEntry entry;
        entry.path = s.path;
        entry.x = s.x;
        entry.y = s.y;
        entry.w = s.w;
        entry.h = s.h;
        entry.trim_left = s.trim_left;
        entry.trim_top = s.trim_top;
        entry.trim_right = s.trim_right;
        entry.trim_bottom = s.trim_bottom;
        entry.rotated = s.rotated;
        seed.entries.push_back(std::move(entry));
    }
    return seed;
}

void prune_all_caches() {
    remove_legacy_top_level_cache_files();

    const fs::path parent = cache_root_dir();
    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    std::unordered_set<std::string> base_paths;
    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("spratlayout_")) {
            continue;
        }

        size_t marker = name.find(".cache.layout.");
        if (marker == std::string::npos) {
            marker = name.find(".cache.seed.");
        }
        if (marker == std::string::npos) {
            continue;
        }

        const std::string base_name = name.substr(0, marker + std::string(".cache").size());
        base_paths.insert((parent / base_name).string());
    }

    for (const std::string& base_path : base_paths) {
        prune_family(base_path, k_cache_max_age_seconds);
    }
}

void prune_cache_family(const fs::path& base_cache_path) {
    prune_family(base_cache_path, k_cache_max_age_seconds);
}

} // namespace sprat::layout
