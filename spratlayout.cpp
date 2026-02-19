// spratlayout.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 spratlayout.cpp -o spratlayout

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <functional>
#include <memory>
#include <limits>
#include <cmath>
#include <utility>
#include <array>
#include <cstddef>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

enum class Mode { POT, COMPACT, FAST };
enum class OptimizeTarget { GPU, SPACE };
enum class Profile { DESKTOP, MOBILE, LEGACY, SPACE, FAST, CSS };

struct Sprite {
    std::string path;
    int w, h;
    int x = 0, y = 0;
    int trim_left = 0, trim_top = 0;
    int trim_right = 0, trim_bottom = 0;
};

struct ImageMeta {
    uintmax_t file_size = 0;
    long long mtime_ticks = 0;
};

struct ImageSource {
    fs::path file_path;
    std::string path;
    ImageMeta meta;
};

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
};

struct LayoutCandidate {
    bool valid = false;
    size_t area = 0;
    int w = 0;
    int h = 0;
    std::vector<Sprite> sprites;
};

struct Node {
    int x, y, w, h;
    bool used = false;
    std::unique_ptr<Node> right;
    std::unique_ptr<Node> down;

    Node(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
};

bool checked_add_int(int a, int b, int& out) {
    if (b > 0 && a > std::numeric_limits<int>::max() - b) {
        return false;
    }
    if (b < 0 && a < std::numeric_limits<int>::min() - b) {
        return false;
    }
    out = a + b;
    return true;
}

bool checked_mul_size_t(size_t a, size_t b, size_t& out) {
    if (a == 0 || b <= std::numeric_limits<size_t>::max() / a) {
        out = a * b;
        return true;
    }
    return false;
}

bool read_image_meta(const fs::path& path, ImageMeta& out) {
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return false;
    }
    fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    out.file_size = size;
    out.mtime_ticks = mtime.time_since_epoch().count();
    return true;
}

long long now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool is_supported_image_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty()) {
        return false;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".tga" || ext == ".gif" || ext == ".psd" || ext == ".pic" ||
           ext == ".pnm" || ext == ".pgm" || ext == ".ppm" || ext == ".hdr" ||
           ext == ".webp";
}

void prune_stale_cache_entries(std::unordered_map<std::string, ImageCacheEntry>& entries,
                               long long now_unix,
                               long long max_age_seconds) {
    for (auto it = entries.begin(); it != entries.end();) {
        const long long cached_at = it->second.cached_at_unix;
        if (cached_at <= 0 || cached_at > now_unix || (now_unix - cached_at) > max_age_seconds) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

bool load_image_cache(const fs::path& cache_path,
                      std::unordered_map<std::string, ImageCacheEntry>& out) {
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
    if (header_tag != "spratlayout_cache" || (version != 1 && version != 2)) {
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
        if (version == 2) {
            if (!(in >> entry.cached_at_unix)) {
                break;
            }
        }
        if (entry.w <= 0 || entry.h <= 0) {
            continue;
        }
        entry.trim_transparent = trim_flag != 0;
        const std::string key = path + (entry.trim_transparent ? "|1" : "|0");
        out[key] = entry;
    }

    return true;
}

bool save_image_cache(const fs::path& cache_path,
                      const std::unordered_map<std::string, ImageCacheEntry>& entries) {
    fs::path tmp = cache_path;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "spratlayout_cache 2\n";
    for (const auto& kv : entries) {
        std::string path = kv.first;
        if (path.size() > 2 &&
            path[path.size() - 2] == '|' &&
            (path.back() == '0' || path.back() == '1')) {
            path = path.substr(0, path.size() - 2);
        }
        const ImageCacheEntry& e = kv.second;
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
            << e.cached_at_unix << "\n";
    }
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

    return fs::path("/tmp");
}

fs::path build_cache_path(const fs::path& folder) {
    std::error_code ec;
    fs::path normalized = fs::absolute(folder, ec);
    std::string folder_key = (!ec ? normalized.lexically_normal().string() : folder.string());
    size_t hash = std::hash<std::string>{}(folder_key);

    std::ostringstream name;
    name << "spratlayout_" << std::hex << hash << ".cache";
    return default_temp_dir() / name.str();
}

std::string to_hex_size_t(size_t value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

bool is_file_older_than_seconds(const fs::path& path, long long max_age_seconds) {
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
        return false;
    }
    long long age = std::chrono::duration_cast<std::chrono::seconds>(now - file_time).count();
    return age > max_age_seconds;
}

std::string build_layout_signature(Profile profile,
                                   Mode mode,
                                   OptimizeTarget optimize_target,
                                   int max_width_limit,
                                   int max_height_limit,
                                   int padding,
                                   double scale,
                                   bool trim_transparent,
                                   const std::vector<ImageSource>& sources) {
    std::vector<std::string> parts;
    parts.reserve(sources.size());
    for (const auto& source : sources) {
        std::ostringstream line;
        line << source.path << "|" << source.meta.file_size << "|" << source.meta.mtime_ticks;
        parts.push_back(line.str());
    }
    std::sort(parts.begin(), parts.end());

    std::ostringstream sig;
    sig << static_cast<int>(profile) << "|"
        << static_cast<int>(mode) << "|"
        << static_cast<int>(optimize_target) << "|"
        << max_width_limit << "|"
        << max_height_limit << "|"
        << padding << "|"
        << std::setprecision(17) << scale << "|"
        << (trim_transparent ? 1 : 0);
    for (const std::string& part : parts) {
        sig << "\n" << part;
    }
    return to_hex_size_t(std::hash<std::string>{}(sig.str()));
}

bool load_output_cache(const fs::path& cache_path,
                       const std::string& expected_signature,
                       std::string& output) {
    std::ifstream in(cache_path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string header;
    if (!std::getline(in, header) || header != "spratlayout_output_cache 1") {
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
    fs::path tmp = cache_path;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "spratlayout_output_cache 1\n";
    out << signature << "\n";
    out << output;
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

Node* insert(Node* node, Sprite& sprite, int w, int h) {
    if (node->used) {
        if (node->right) {
            if (Node* r = insert(node->right.get(), sprite, w, h)) {
                return r;
            }
        }
        if (node->down) {
            return insert(node->down.get(), sprite, w, h);
        }
        return nullptr;
    }
    if (w > node->w || h > node->h) {
        return nullptr;
    }
    if (w == node->w && h == node->h) {
        node->used = true;
        return node;
    }
    node->used = true;
    node->down = std::make_unique<Node>(node->x, node->y + h, node->w, node->h - h);
    node->right = std::make_unique<Node>(node->x + w, node->y, node->w - w, h);
    return node;
}

bool try_pack(std::unique_ptr<Node>& root, std::vector<Sprite>& sprites, int padding = 0) {
    root->used = false;
    root->right.reset();
    root->down.reset();
    for (auto& sprite : sprites) {
        int w = 0;
        int h = 0;
        if (
            !checked_add_int(sprite.w, padding, w)
            || !checked_add_int(sprite.h, padding, h)
        ) {
            return false;
        }
        Node* node = insert(root.get(), sprite, w, h);
        if (!node) {
            return false;
        }
        sprite.x = node->x;
        sprite.y = node->y;
    }
    return true;
}

enum class SortMode {
    Height,
    Width,
    Area,
    MaxSide,
    Perimeter
};

bool sort_sprites_by_mode(std::vector<Sprite>& sprites, SortMode mode) {
    auto cmp_height = [](const Sprite& a, const Sprite& b) {
        if (a.h != b.h) return a.h > b.h;
        return a.w > b.w;
    };
    auto cmp_width = [](const Sprite& a, const Sprite& b) {
        if (a.w != b.w) return a.w > b.w;
        return a.h > b.h;
    };
    auto cmp_area = [](const Sprite& a, const Sprite& b) {
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        if (area_a != area_b) return area_a > area_b;
        if (a.h != b.h) return a.h > b.h;
        return a.w > b.w;
    };
    auto cmp_max_side = [](const Sprite& a, const Sprite& b) {
        int max_a = std::max(a.w, a.h);
        int max_b = std::max(b.w, b.h);
        if (max_a != max_b) return max_a > max_b;
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };
    auto cmp_perimeter = [](const Sprite& a, const Sprite& b) {
        int p_a = a.w + a.h;
        int p_b = b.w + b.h;
        if (p_a != p_b) return p_a > p_b;
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };

    switch (mode) {
        case SortMode::Height:
            std::sort(sprites.begin(), sprites.end(), cmp_height);
            return true;
        case SortMode::Width:
            std::sort(sprites.begin(), sprites.end(), cmp_width);
            return true;
        case SortMode::Area:
            std::sort(sprites.begin(), sprites.end(), cmp_area);
            return true;
        case SortMode::MaxSide:
            std::sort(sprites.begin(), sprites.end(), cmp_max_side);
            return true;
        case SortMode::Perimeter:
            std::sort(sprites.begin(), sprites.end(), cmp_perimeter);
            return true;
    }
    return false;
}

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class RectHeuristic {
    BestShortSideFit,
    BestAreaFit,
    BottomLeft
};

bool rects_intersect(const Rect& a, const Rect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

bool rect_contains(const Rect& a, const Rect& b) {
    return b.x >= a.x && b.y >= a.y &&
           b.x + b.w <= a.x + a.w &&
           b.y + b.h <= a.y + a.h;
}

bool split_free_rect(const Rect& free_rect, const Rect& used_rect, std::vector<Rect>& out) {
    if (!rects_intersect(free_rect, used_rect)) {
        out.push_back(free_rect);
        return true;
    }

    int free_right = free_rect.x + free_rect.w;
    int free_bottom = free_rect.y + free_rect.h;
    int used_right = used_rect.x + used_rect.w;
    int used_bottom = used_rect.y + used_rect.h;

    if (used_rect.x > free_rect.x) {
        out.push_back({free_rect.x, free_rect.y, used_rect.x - free_rect.x, free_rect.h});
    }
    if (used_right < free_right) {
        out.push_back({used_right, free_rect.y, free_right - used_right, free_rect.h});
    }
    if (used_rect.y > free_rect.y) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            out.push_back({x0, free_rect.y, x1 - x0, used_rect.y - free_rect.y});
        }
    }
    if (used_bottom < free_bottom) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            out.push_back({x0, used_bottom, x1 - x0, free_bottom - used_bottom});
        }
    }
    return true;
}

void prune_free_rects(std::vector<Rect>& free_rects) {
    size_t i = 0;
    while (i < free_rects.size()) {
        bool removed_i = false;
        size_t j = i + 1;
        while (j < free_rects.size()) {
            if (rect_contains(free_rects[i], free_rects[j])) {
                free_rects.erase(free_rects.begin() + static_cast<std::ptrdiff_t>(j));
                continue;
            }
            if (rect_contains(free_rects[j], free_rects[i])) {
                free_rects.erase(free_rects.begin() + static_cast<std::ptrdiff_t>(i));
                removed_i = true;
                break;
            }
            ++j;
        }
        if (!removed_i) {
            ++i;
        }
    }
}

bool pack_compact_maxrects(
    std::vector<Sprite>& sprites,
    int width_limit,
    int padding,
    int max_height,
    RectHeuristic heuristic,
    int& out_width,
    int& out_height
) {
    if (width_limit <= 0 || max_height <= 0) {
        return false;
    }

    std::vector<Rect> free_rects;
    free_rects.push_back({0, 0, width_limit, max_height});

    int used_w = 0;
    int used_h = 0;

    for (auto& s : sprites) {
        int rw = 0;
        int rh = 0;
        if (
            !checked_add_int(s.w, padding, rw)
            || !checked_add_int(s.h, padding, rh)
            || rw <= 0 || rh <= 0 || rw > width_limit || rh > max_height
        ) {
            return false;
        }

        int best_index = -1;
        int best_short_fit = std::numeric_limits<int>::max();
        int best_long_fit = std::numeric_limits<int>::max();
        int best_area_fit = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_left = std::numeric_limits<int>::max();

        for (size_t i = 0; i < free_rects.size(); ++i) {
            const Rect& fr = free_rects[i];
            if (rw > fr.w || rh > fr.h) {
                continue;
            }

            int leftover_h = fr.h - rh;
            int leftover_w = fr.w - rw;
            int short_fit = std::min(leftover_h, leftover_w);
            int long_fit = std::max(leftover_h, leftover_w);
            int area_fit = leftover_h * leftover_w;

            bool better = false;
            if (heuristic == RectHeuristic::BestShortSideFit) {
                better = short_fit < best_short_fit ||
                         (short_fit == best_short_fit && long_fit < best_long_fit) ||
                         (short_fit == best_short_fit && long_fit == best_long_fit && fr.y < best_top) ||
                         (short_fit == best_short_fit && long_fit == best_long_fit && fr.y == best_top && fr.x < best_left);
            } else if (heuristic == RectHeuristic::BestAreaFit) {
                better = area_fit < best_area_fit ||
                         (area_fit == best_area_fit && short_fit < best_short_fit) ||
                         (area_fit == best_area_fit && short_fit == best_short_fit && fr.y < best_top) ||
                         (area_fit == best_area_fit && short_fit == best_short_fit && fr.y == best_top && fr.x < best_left);
            } else {
                better = fr.y < best_top || (fr.y == best_top && fr.x < best_left) ||
                         (fr.y == best_top && fr.x == best_left && short_fit < best_short_fit);
            }

            if (better) {
                best_index = static_cast<int>(i);
                best_short_fit = short_fit;
                best_long_fit = long_fit;
                best_area_fit = area_fit;
                best_top = fr.y;
                best_left = fr.x;
            }
        }

        if (best_index < 0) {
            return false;
        }

        Rect used = {free_rects[static_cast<size_t>(best_index)].x,
                     free_rects[static_cast<size_t>(best_index)].y,
                     rw, rh};
        s.x = used.x;
        s.y = used.y;

        if (used.x + used.w > used_w) used_w = used.x + used.w;
        if (used.y + used.h > used_h) used_h = used.y + used.h;

        std::vector<Rect> next_free;
        next_free.reserve(free_rects.size() * 2);
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }

        free_rects.clear();
        free_rects.reserve(next_free.size());
        for (const auto& r : next_free) {
            if (r.w > 0 && r.h > 0) {
                free_rects.push_back(r);
            }
        }
        prune_free_rects(free_rects);
    }

    out_width = used_w;
    out_height = used_h;
    return out_width > 0 && out_height > 0;
}

bool pack_fast_shelf(
    std::vector<Sprite>& sprites,
    int max_row_width,
    int padding,
    int& out_width,
    int& out_height
) {
    int x = 0;
    int y = 0;
    int row_height = 0;
    int atlas_width = 0;
    if (max_row_width <= 0) {
        return false;
    }

    for (auto& s : sprites) {
        int w = 0;
        int h = 0;
        int candidate_x = 0;
        int next_y = 0;

        if (
            !checked_add_int(s.w, padding, w)
            || !checked_add_int(s.h, padding, h)
            || w <= 0 || h <= 0
            || w > max_row_width
            || !checked_add_int(x, w, candidate_x)
        ) {
            return false;
        }
        
        if (x > 0 && candidate_x > max_row_width) {
            if (!checked_add_int(y, row_height, next_y)) {
                return false;
            }
            y = next_y;
            x = 0;
            row_height = 0;
            if (!checked_add_int(x, w, candidate_x)) {
                return false;
            }
        }

        s.x = x;
        s.y = y;
        x = candidate_x;
        if (h > row_height) {
            row_height = h;
        }
        if (x > atlas_width) {
            atlas_width = x;
        }
    }

    int total_height = 0;
    if (!checked_add_int(y, row_height, total_height)) {
        return false;
    }
    out_width = atlas_width;
    out_height = total_height;
    return out_width > 0 && out_height > 0;
}

int next_power_of_two(int v) {
    if (v <= 1) {
        return 1;
    }

    int p = 1;
    while (p < v) {
        if (p > std::numeric_limits<int>::max() / 2) {
            return -1;
        }
        p <<= 1;
    }
    return p;
}

bool pick_better_layout_candidate(
    size_t candidate_area,
    int candidate_w,
    int candidate_h,
    bool have_best,
    size_t best_area,
    int best_w,
    int best_h,
    OptimizeTarget optimize_target
) {
    if (!have_best) {
        return true;
    }

    int candidate_max_side = std::max(candidate_w, candidate_h);
    int best_max_side = std::max(best_w, best_h);
    int candidate_aspect_delta = std::abs(candidate_w - candidate_h);
    int best_aspect_delta = std::abs(best_w - best_h);

    if (optimize_target == OptimizeTarget::GPU) {
        if (candidate_max_side != best_max_side) {
            return candidate_max_side < best_max_side;
        }
        if (candidate_aspect_delta != best_aspect_delta) {
            return candidate_aspect_delta < best_aspect_delta;
        }
        if (candidate_area != best_area) {
            return candidate_area < best_area;
        }
        return candidate_w < best_w;
    }

    if (candidate_area != best_area) {
        return candidate_area < best_area;
    }

    if (candidate_max_side != best_max_side) {
        return candidate_max_side < best_max_side;
    }

    if (candidate_aspect_delta != best_aspect_delta) {
        return candidate_aspect_delta < best_aspect_delta;
    }

    return candidate_w < best_w;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: spratlayout <folder> [--profile desktop|mobile|legacy|space|fast|css] [--max-width N] [--max-height N] [--padding N] [--scale F] [--trim-transparent] [--threads N]\n";
        return 1;
    }

    fs::path folder = argv[1];
    Profile profile = Profile::DESKTOP;
    Mode mode = Mode::COMPACT;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    int max_width_limit = 0;
    int max_height_limit = 0;
    bool has_max_width_limit = false;
    bool has_max_height_limit = false;
    int padding = 0;
    double scale = 1.0;
    bool trim_transparent = false;
    unsigned int thread_limit = 0;

    // parse args
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "desktop") {
                profile = Profile::DESKTOP;
            } else if (val == "mobile") {
                profile = Profile::MOBILE;
            } else if (val == "legacy") {
                profile = Profile::LEGACY;
            } else if (val == "space") {
                profile = Profile::SPACE;
            } else if (val == "fast") {
                profile = Profile::FAST;
            } else if (val == "css") {
                profile = Profile::CSS;
            } else {
                std::cerr << "Invalid profile\n";
                return 1;
            }
        } else if (arg == "--max-width" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                max_width_limit = std::stoi(value, &idx);
                if (idx != value.size() || max_width_limit <= 0) {
                    std::cerr << "Invalid max width value: " << value << "\n";
                    return 1;
                }
                has_max_width_limit = true;
            } catch (const std::exception&) {
                std::cerr << "Invalid max width value: " << value << "\n";
                return 1;
            }
        } else if (arg == "--max-height" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                max_height_limit = std::stoi(value, &idx);
                if (idx != value.size() || max_height_limit <= 0) {
                    std::cerr << "Invalid max height value: " << value << "\n";
                    return 1;
                }
                has_max_height_limit = true;
            } catch (const std::exception&) {
                std::cerr << "Invalid max height value: " << value << "\n";
                return 1;
            }
        } else if (arg == "--padding" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                padding = std::stoi(value, &idx);
                if (idx != value.size()) {
                    std::cerr << "Invalid padding value: " << value << "\n";
                    return 1;
                }
            } catch (const std::exception&) {
                std::cerr << "Invalid padding value: " << value << "\n";
                return 1;
            }
            if (padding < 0) {
                padding = 0;
            }
        } else if (arg == "--scale" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                scale = std::stod(value, &idx);
                if (idx != value.size() || !std::isfinite(scale) || scale <= 0.0) {
                    std::cerr << "Invalid scale value: " << value << "\n";
                    return 1;
                }
            } catch (const std::exception&) {
                std::cerr << "Invalid scale value: " << value << "\n";
                return 1;
            }
        } else if (arg == "--trim-transparent") {
            trim_transparent = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                int parsed = std::stoi(value, &idx);
                if (idx != value.size() || parsed <= 0) {
                    std::cerr << "Invalid thread count: " << value << "\n";
                    return 1;
                }
                thread_limit = static_cast<unsigned int>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid thread count: " << value << "\n";
                return 1;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    // Resolve algorithm strategy from the selected profile.
    switch (profile) {
        case Profile::DESKTOP:
            mode = Mode::COMPACT;
            optimize_target = OptimizeTarget::GPU;
            break;
        case Profile::MOBILE:
            mode = Mode::COMPACT;
            optimize_target = OptimizeTarget::GPU;
            if (!has_max_width_limit) {
                max_width_limit = 2048;
            }
            if (!has_max_height_limit) {
                max_height_limit = 2048;
            }
            break;
        case Profile::LEGACY:
            mode = Mode::POT;
            optimize_target = OptimizeTarget::SPACE;
            if (!has_max_width_limit) {
                max_width_limit = 1024;
            }
            if (!has_max_height_limit) {
                max_height_limit = 1024;
            }
            break;
        case Profile::SPACE:
            mode = Mode::COMPACT;
            optimize_target = OptimizeTarget::SPACE;
            break;
        case Profile::FAST:
            mode = Mode::FAST;
            optimize_target = OptimizeTarget::GPU;
            break;
        case Profile::CSS:
            mode = Mode::FAST;
            optimize_target = OptimizeTarget::SPACE;
            break;
    }

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "Error: invalid directory\n";
        return 1;
    }

    const fs::path cache_path = build_cache_path(folder);
    const fs::path output_cache_path = cache_path.string() + ".layout";
    constexpr long long k_cache_max_age_seconds = 3600;
    const long long now_unix = now_unix_seconds();

    std::vector<ImageSource> sources;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const fs::path file_path = entry.path();
        if (!is_supported_image_extension(file_path)) {
            continue;
        }
        ImageMeta meta;
        if (!read_image_meta(file_path, meta)) {
            continue;
        }
        ImageSource source;
        source.file_path = file_path;
        source.path = file_path.string();
        source.meta = meta;
        sources.push_back(std::move(source));
    }

    const std::string layout_signature = build_layout_signature(
        profile, mode, optimize_target, max_width_limit, max_height_limit,
        padding, scale, trim_transparent, sources);
    if (!is_file_older_than_seconds(output_cache_path, k_cache_max_age_seconds)) {
        std::string cached_output;
        if (load_output_cache(output_cache_path, layout_signature, cached_output)) {
            std::cout << cached_output;
            return 0;
        }
    }

    std::unordered_map<std::string, ImageCacheEntry> cache_entries;
    load_image_cache(cache_path, cache_entries);
    prune_stale_cache_entries(cache_entries, now_unix, k_cache_max_age_seconds);

    std::vector<Sprite> sprites;
    for (const auto& source : sources) {
        const fs::path& file_path = source.file_path;
        const std::string& path = source.path;
        const ImageMeta& meta = source.meta;

        const std::string cache_key = path + (trim_transparent ? "|1" : "|0");
        auto cache_it = cache_entries.find(cache_key);
        if (cache_it != cache_entries.end()) {
            const ImageCacheEntry& cached = cache_it->second;
            if (cached.trim_transparent == trim_transparent &&
                cached.file_size == meta.file_size &&
                cached.mtime_ticks == meta.mtime_ticks) {
                Sprite s;
                s.path = path;
                s.w = cached.w;
                s.h = cached.h;
                s.trim_left = cached.trim_left;
                s.trim_top = cached.trim_top;
                s.trim_right = cached.trim_right;
                s.trim_bottom = cached.trim_bottom;
                sprites.push_back(std::move(s));
                cache_it->second.cached_at_unix = now_unix;
                continue;
            }
        }

        Sprite loaded_sprite;
        loaded_sprite.path = path;
        if (!trim_transparent) {
            int w, h, channels;
            if (!stbi_info(path.c_str(), &w, &h, &channels)) {
                continue;
            }
            loaded_sprite.w = w;
            loaded_sprite.h = h;
            sprites.push_back(loaded_sprite);
            cache_entries[cache_key] = ImageCacheEntry{
                trim_transparent,
                meta.file_size,
                meta.mtime_ticks,
                loaded_sprite.w,
                loaded_sprite.h,
                loaded_sprite.trim_left,
                loaded_sprite.trim_top,
                loaded_sprite.trim_right,
                loaded_sprite.trim_bottom,
                now_unix
            };
            continue;
        }

        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data) {
            continue;
        }

        int min_x = w;
        int min_y = h;
        int max_x = -1;
        int max_y = -1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t pixel_index = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                size_t alpha_index = pixel_index * static_cast<size_t>(4) + static_cast<size_t>(3);
                if (data[alpha_index] == 0) {
                    continue;
                }
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }

        if (max_x >= min_x && max_y >= min_y) {
            loaded_sprite.trim_left = min_x;
            loaded_sprite.trim_top = min_y;
            loaded_sprite.trim_right = (w - 1) - max_x;
            loaded_sprite.trim_bottom = (h - 1) - max_y;
            loaded_sprite.w = max_x - min_x + 1;
            loaded_sprite.h = max_y - min_y + 1;
        } else {
            // Fully transparent image: keep a 1x1 transparent region.
            loaded_sprite.trim_left = 0;
            loaded_sprite.trim_top = 0;
            loaded_sprite.trim_right = std::max(0, w - 1);
            loaded_sprite.trim_bottom = std::max(0, h - 1);
            loaded_sprite.w = 1;
            loaded_sprite.h = 1;
        }

        stbi_image_free(data);
        sprites.push_back(loaded_sprite);
        cache_entries[cache_key] = ImageCacheEntry{
            trim_transparent,
            meta.file_size,
            meta.mtime_ticks,
            loaded_sprite.w,
            loaded_sprite.h,
            loaded_sprite.trim_left,
            loaded_sprite.trim_top,
            loaded_sprite.trim_right,
            loaded_sprite.trim_bottom,
            now_unix
        };
    }

    save_image_cache(cache_path, cache_entries);

    if (sprites.empty()) {
        std::cerr << "Error: no valid images found\n";
        return 1;
    }

    if (scale != 1.0) {
        for (auto& s : sprites) {
            int scaled_w = 0;
            int scaled_h = 0;
            if (!scale_dimension(s.w, scale, scaled_w) || !scale_dimension(s.h, scale, scaled_h)) {
                std::cerr << "Error: scaled sprite dimensions are invalid\n";
                return 1;
            }
            s.w = scaled_w;
            s.h = scaled_h;
        }
    }

    int max_width = 0;
    int max_height = 0;
    int sum_width = 0;
    int sum_height = 0;
    size_t total_area = 0;
    for (auto& s : sprites) {
        int padded_w = 0;
        int padded_h = 0;
        if (!checked_add_int(s.w, padding, padded_w)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }
        if (!checked_add_int(s.h, padding, padded_h)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }

        size_t sprite_area = 0;
        if (!checked_mul_size_t(static_cast<size_t>(padded_w), static_cast<size_t>(padded_h), sprite_area) ||
            sprite_area > std::numeric_limits<size_t>::max() - total_area) {
            std::cerr << "Error: total area is too large\n";
            return 1;
        }
        total_area += sprite_area;

        max_width = std::max(max_width, padded_w);
        max_height = std::max(max_height, padded_h);
        if (!checked_add_int(sum_width, padded_w, sum_width) ||
            !checked_add_int(sum_height, padded_h, sum_height)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }
    }

    int atlas_width = 0, atlas_height = 0;
    int width_upper_bound = sum_width;
    int height_upper_bound = sum_height;
    if (max_width_limit > 0) {
        width_upper_bound = std::min(width_upper_bound, max_width_limit);
    }
    if (max_height_limit > 0) {
        height_upper_bound = std::min(height_upper_bound, max_height_limit);
    }
    if (max_width > width_upper_bound || max_height > height_upper_bound) {
        std::cerr << "Error: sprite dimensions exceed provided atlas limits\n";
        return 1;
    }

    std::unique_ptr<Node> root;
    const std::array<SortMode, 5> sort_modes = {
        SortMode::Area,
        SortMode::MaxSide,
        SortMode::Height,
        SortMode::Width,
        SortMode::Perimeter
    };
    const std::array<RectHeuristic, 3> rect_heuristics = {
        RectHeuristic::BestShortSideFit,
        RectHeuristic::BestAreaFit,
        RectHeuristic::BottomLeft
    };

    if (mode == Mode::POT) {
        int min_pot_width = next_power_of_two(max_width);
        int min_pot_height = next_power_of_two(max_height);
        if (min_pot_width <= 0 || min_pot_height <= 0) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }

        // First, find an upper bound that can pack, then search all POT
        // rectangles up to that area and pick the least wasteful successful fit.
        int side = std::max(min_pot_width, min_pot_height);
        std::vector<Sprite> best_sprites = sprites;
        int best_w = 0;
        int best_h = 0;
        size_t best_area = 0;
        size_t max_candidate_area = 0;
        bool have_best = false;

        while (true) {
            if (max_width_limit > 0 && side > max_width_limit) {
                std::cerr << "Error: no POT layout fits within max width\n";
                return 1;
            }
            if (max_height_limit > 0 && side > max_height_limit) {
                std::cerr << "Error: no POT layout fits within max height\n";
                return 1;
            }
            for (SortMode sort_mode : sort_modes) {
                std::vector<Sprite> trial_sprites = sprites;
                sort_sprites_by_mode(trial_sprites, sort_mode);
                root = std::make_unique<Node>(0, 0, side, side);
                if (!try_pack(root, trial_sprites, padding)) {
                    continue;
                }
                size_t area = static_cast<size_t>(side) * static_cast<size_t>(side);
                best_sprites = std::move(trial_sprites);
                best_w = side;
                best_h = side;
                best_area = area;
                max_candidate_area = area;
                have_best = true;
                break;
            }
            if (have_best) {
                break;
            }
            if (side > std::numeric_limits<int>::max() / 2) {
                std::cerr << "Error: atlas dimensions overflow\n";
                return 1;
            }
            side *= 2;
        }

        std::vector<int> pot_widths;
        std::vector<int> pot_heights;
        for (int w = min_pot_width; w > 0 && static_cast<size_t>(w) <= best_area; w *= 2) {
            pot_widths.push_back(w);
            if (w > std::numeric_limits<int>::max() / 2) {
                break;
            }
        }
        for (int h = min_pot_height; h > 0 && static_cast<size_t>(h) <= best_area; h *= 2) {
            pot_heights.push_back(h);
            if (h > std::numeric_limits<int>::max() / 2) {
                break;
            }
        }

        for (int w : pot_widths) {
            for (int h : pot_heights) {
                size_t area = static_cast<size_t>(w) * static_cast<size_t>(h);
                if (area > max_candidate_area) {
                    continue;
                }
                if (max_width_limit > 0 && w > max_width_limit) {
                    continue;
                }
                if (max_height_limit > 0 && h > max_height_limit) {
                    continue;
                }
                if (!pick_better_layout_candidate(area, w, h, have_best, best_area, best_w, best_h, optimize_target)) {
                    continue;
                }

                for (SortMode sort_mode : sort_modes) {
                    std::vector<Sprite> trial_sprites = sprites;
                    sort_sprites_by_mode(trial_sprites, sort_mode);
                    root = std::make_unique<Node>(0, 0, w, h);
                    if (!try_pack(root, trial_sprites, padding)) {
                        continue;
                    }

                    best_sprites = std::move(trial_sprites);
                    best_w = w;
                    best_h = h;
                    best_area = area;
                    have_best = true;
                    break;
                }
            }
        }

        if (!have_best) {
            std::cerr << "Error: failed to compute pot layout\n";
            return 1;
        }

        sprites = std::move(best_sprites);
        atlas_width = best_w;
        atlas_height = best_h;
    } else if (mode == Mode::COMPACT) {
        if (sum_width <= 0 || sum_height <= 0) {
            std::cerr << "Error: compact bounds are invalid\n";
            return 1;
        }

        int seed_width = max_width;
        if (total_area > 0) {
            long double area_root = std::sqrt(static_cast<long double>(total_area));
            if (area_root > static_cast<long double>(std::numeric_limits<int>::max())) {
                std::cerr << "Error: compact width is too large\n";
                return 1;
            }
            int root_width = static_cast<int>(std::ceil(area_root));
            if (root_width > seed_width) {
                seed_width = root_width;
            }
        }
        if (seed_width > width_upper_bound) {
            seed_width = width_upper_bound;
        }
        if (seed_width < max_width) {
            seed_width = max_width;
        }

        size_t best_area = std::numeric_limits<size_t>::max();
        int best_w = 0;
        int best_h = 0;
        std::vector<Sprite> best_sprites;
        bool have_best = false;
        auto consider_candidate = [&](LayoutCandidate&& candidate) {
            if (!candidate.valid) {
                return;
            }
            if (!have_best || pick_better_layout_candidate(
                                  candidate.area, candidate.w, candidate.h,
                                  true, best_area, best_w, best_h,
                                  optimize_target)) {
                best_area = candidate.area;
                best_w = candidate.w;
                best_h = candidate.h;
                best_sprites = std::move(candidate.sprites);
                have_best = true;
            }
        };

        for (SortMode sort_mode : sort_modes) {
            for (RectHeuristic rect_heuristic : rect_heuristics) {
                std::vector<Sprite> seed_sprites = sprites;
                sort_sprites_by_mode(seed_sprites, sort_mode);
                int seed_used_w = 0;
                int seed_used_h = 0;
                if (!pack_compact_maxrects(seed_sprites, seed_width, padding, height_upper_bound, rect_heuristic, seed_used_w, seed_used_h)) {
                    continue;
                }
                size_t seed_area = static_cast<size_t>(seed_used_w) * static_cast<size_t>(seed_used_h);
                LayoutCandidate seed_candidate;
                seed_candidate.valid = true;
                seed_candidate.area = seed_area;
                seed_candidate.w = seed_used_w;
                seed_candidate.h = seed_used_h;
                seed_candidate.sprites = std::move(seed_sprites);
                consider_candidate(std::move(seed_candidate));
            }
        }

        if (!have_best) {
            std::cerr << "Error: failed to compute compact layout\n";
            return 1;
        }

        const int width_count = width_upper_bound - max_width + 1;
        const size_t area_upper_bound = best_area;
        unsigned int worker_count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
        if (worker_count == 0) {
            worker_count = 1;
        }
        worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(std::max(1, width_count)));

        std::vector<std::thread> compact_workers;
        std::vector<LayoutCandidate> compact_candidates(worker_count);
        compact_workers.reserve(worker_count);
        for (unsigned int worker_index = 0; worker_index < worker_count; ++worker_index) {
            compact_workers.emplace_back([&, worker_index]() {
                const int begin = max_width + static_cast<int>((static_cast<long long>(width_count) * worker_index) / worker_count);
                const int end = max_width + static_cast<int>((static_cast<long long>(width_count) * (worker_index + 1)) / worker_count) - 1;
                LayoutCandidate local_best;
                for (int width = begin; width <= end; ++width) {
                    if (optimize_target == OptimizeTarget::SPACE && total_area > 0) {
                        size_t min_height = (total_area + static_cast<size_t>(width) - 1) / static_cast<size_t>(width);
                        size_t lower_bound_area = static_cast<size_t>(width) * min_height;
                        if (lower_bound_area > area_upper_bound) {
                            continue;
                        }
                    }

                    for (SortMode sort_mode : sort_modes) {
                        for (RectHeuristic rect_heuristic : rect_heuristics) {
                            std::vector<Sprite> trial_sprites = sprites;
                            sort_sprites_by_mode(trial_sprites, sort_mode);
                            int used_w = 0;
                            int used_h = 0;
                            if (!pack_compact_maxrects(trial_sprites, width, padding, height_upper_bound, rect_heuristic, used_w, used_h)) {
                                continue;
                            }
                            size_t area = static_cast<size_t>(used_w) * static_cast<size_t>(used_h);
                            if (!local_best.valid ||
                                pick_better_layout_candidate(area, used_w, used_h, true,
                                                             local_best.area, local_best.w, local_best.h,
                                                             optimize_target)) {
                                local_best.valid = true;
                                local_best.area = area;
                                local_best.w = used_w;
                                local_best.h = used_h;
                                local_best.sprites = std::move(trial_sprites);
                            }
                        }
                    }
                }
                compact_candidates[worker_index] = std::move(local_best);
            });
        }
        for (auto& worker : compact_workers) {
            worker.join();
        }
        for (auto& candidate : compact_candidates) {
            consider_candidate(std::move(candidate));
        }

        // For GPU-focused optimization, also evaluate shelf candidates and keep
        // whichever result is better for GPU shape.
        if (optimize_target == OptimizeTarget::GPU) {
            std::vector<std::thread> shelf_workers;
            std::vector<LayoutCandidate> shelf_candidates(worker_count);
            shelf_workers.reserve(worker_count);
            for (unsigned int worker_index = 0; worker_index < worker_count; ++worker_index) {
                shelf_workers.emplace_back([&, worker_index]() {
                    const int begin = max_width + static_cast<int>((static_cast<long long>(width_count) * worker_index) / worker_count);
                    const int end = max_width + static_cast<int>((static_cast<long long>(width_count) * (worker_index + 1)) / worker_count) - 1;
                    LayoutCandidate local_best;
                    for (int width = begin; width <= end; ++width) {
                        for (SortMode sort_mode : sort_modes) {
                            std::vector<Sprite> shelf_sprites = sprites;
                            sort_sprites_by_mode(shelf_sprites, sort_mode);
                            int shelf_w = 0;
                            int shelf_h = 0;
                            if (!pack_fast_shelf(shelf_sprites, width, padding, shelf_w, shelf_h)) {
                                continue;
                            }
                            if (shelf_h > height_upper_bound) {
                                continue;
                            }
                            size_t shelf_area = static_cast<size_t>(shelf_w) * static_cast<size_t>(shelf_h);
                            if (!local_best.valid ||
                                pick_better_layout_candidate(shelf_area, shelf_w, shelf_h, true,
                                                             local_best.area, local_best.w, local_best.h,
                                                             optimize_target)) {
                                local_best.valid = true;
                                local_best.area = shelf_area;
                                local_best.w = shelf_w;
                                local_best.h = shelf_h;
                                local_best.sprites = std::move(shelf_sprites);
                            }
                        }
                    }
                    shelf_candidates[worker_index] = std::move(local_best);
                });
            }
            for (auto& worker : shelf_workers) {
                worker.join();
            }
            for (auto& candidate : shelf_candidates) {
                consider_candidate(std::move(candidate));
            }

            // Hard safeguard: compare against the exact FAST baseline candidate.
            int fast_target_width = max_width;
            if (total_area > 0) {
                long double area_root = std::sqrt(static_cast<long double>(total_area));
                if (area_root <= static_cast<long double>(std::numeric_limits<int>::max())) {
                    int candidate = static_cast<int>(std::ceil(area_root));
                    if (candidate > fast_target_width) {
                        fast_target_width = candidate;
                    }
                }
            }
            if (fast_target_width > width_upper_bound) {
                fast_target_width = width_upper_bound;
            }
            if (fast_target_width >= max_width) {
                std::vector<Sprite> fast_baseline = sprites;
                sort_sprites_by_mode(fast_baseline, SortMode::Height);
                int fast_w = 0;
                int fast_h = 0;
                if (pack_fast_shelf(fast_baseline, fast_target_width, padding, fast_w, fast_h) &&
                    fast_h <= height_upper_bound) {
                    size_t fast_area = static_cast<size_t>(fast_w) * static_cast<size_t>(fast_h);
                    LayoutCandidate fast_candidate;
                    fast_candidate.valid = true;
                    fast_candidate.area = fast_area;
                    fast_candidate.w = fast_w;
                    fast_candidate.h = fast_h;
                    fast_candidate.sprites = std::move(fast_baseline);
                    consider_candidate(std::move(fast_candidate));
                }
            }
        }

        sprites = std::move(best_sprites);
        atlas_width = best_w;
        atlas_height = best_h;
    } else {
        int target_width = max_width;
        if (total_area > 0) {
            long double area_root = std::sqrt(static_cast<long double>(total_area));
            if (area_root > static_cast<long double>(std::numeric_limits<int>::max())) {
                std::cerr << "Error: fast width is too large\n";
                return 1;
            }
            int candidate = static_cast<int>(std::ceil(area_root));
            if (candidate > target_width) {
                target_width = candidate;
            }
        }
        if (target_width > width_upper_bound) {
            target_width = width_upper_bound;
        }

        bool packed = false;
        for (int width = target_width; width <= width_upper_bound; ++width) {
            std::vector<Sprite> trial_sprites = sprites;
            sort_sprites_by_mode(trial_sprites, SortMode::Height);
            int packed_width = 0;
            int packed_height = 0;
            if (!pack_fast_shelf(trial_sprites, width, padding, packed_width, packed_height)) {
                continue;
            }
            if (packed_height > height_upper_bound) {
                continue;
            }
            sprites = std::move(trial_sprites);
            atlas_width = packed_width;
            atlas_height = packed_height;
            packed = true;
            break;
        }
        if (!packed) {
            std::cerr << "Error: failed to compute fast layout\n";
            return 1;
        }
    }

    // Output layout
    std::ostringstream output;
    output << "atlas " << atlas_width << "," << atlas_height << "\n";
    output << "scale " << std::setprecision(8) << scale << "\n";
    for (auto& s : sprites) {
        std::string path = s.path;
        size_t pos = 0;
        while ((pos = path.find('"', pos)) != std::string::npos) {
            path.insert(pos, "\\");
            pos += 2;
        }
        output << "sprite \"" << path << "\" "
               << s.x << "," << s.y << " "
               << s.w << "," << s.h;
        if (trim_transparent) {
            output << " " << s.trim_left << "," << s.trim_top
                   << " " << s.trim_right << "," << s.trim_bottom;
        }
        output << "\n";
    }
    const std::string output_text = output.str();
    std::cout << output_text;
    save_output_cache(output_cache_path, layout_signature, output_text);

    return 0;
}
