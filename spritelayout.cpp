// spritelayout.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 spritelayout.cpp -o spritelayout

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <limits>
#include <cmath>
#include <utility>
#include <array>
#include <cstddef>
#include <iomanip>

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
        std::cerr << "Usage: spritelayout <folder> [--profile desktop|mobile|legacy|space|fast|css] [--max-width N] [--max-height N] [--padding N] [--scale F] [--trim-transparent]\n";
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

    std::vector<Sprite> sprites;
    for (auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string path = entry.path().string();
        if (!trim_transparent) {
            int w, h, channels;
            if (!stbi_info(path.c_str(), &w, &h, &channels)) {
                continue;
            }
            sprites.push_back({path, w, h});
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

        Sprite s;
        s.path = path;
        if (max_x >= min_x && max_y >= min_y) {
            s.trim_left = min_x;
            s.trim_top = min_y;
            s.trim_right = (w - 1) - max_x;
            s.trim_bottom = (h - 1) - max_y;
            s.w = max_x - min_x + 1;
            s.h = max_y - min_y + 1;
        } else {
            // Fully transparent image: keep a 1x1 transparent region.
            s.trim_left = 0;
            s.trim_top = 0;
            s.trim_right = std::max(0, w - 1);
            s.trim_bottom = std::max(0, h - 1);
            s.w = 1;
            s.h = 1;
        }

        stbi_image_free(data);
        sprites.push_back(std::move(s));
    }

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
                if (!have_best || pick_better_layout_candidate(seed_area, seed_used_w, seed_used_h, have_best, best_area, best_w, best_h, optimize_target)) {
                    best_area = seed_area;
                    best_w = seed_used_w;
                    best_h = seed_used_h;
                    best_sprites = std::move(seed_sprites);
                    have_best = true;
                }
            }
        }

        if (!have_best) {
            std::cerr << "Error: failed to compute compact layout\n";
            return 1;
        }

        for (int width = max_width; width <= width_upper_bound; ++width) {
            if (optimize_target == OptimizeTarget::SPACE && total_area > 0) {
                size_t min_height = (total_area + static_cast<size_t>(width) - 1) / static_cast<size_t>(width);
                size_t lower_bound_area = static_cast<size_t>(width) * min_height;
                if (lower_bound_area > best_area) {
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
                    if (pick_better_layout_candidate(area, used_w, used_h, true, best_area, best_w, best_h, optimize_target)) {
                        best_area = area;
                        best_w = used_w;
                        best_h = used_h;
                        best_sprites = std::move(trial_sprites);
                    }
                }
            }
        }

        // For GPU-focused optimization, also evaluate shelf candidates and keep
        // whichever result is better for GPU shape.
        if (optimize_target == OptimizeTarget::GPU) {
            for (int width = max_width; width <= width_upper_bound; ++width) {
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
                    if (pick_better_layout_candidate(
                            shelf_area, shelf_w, shelf_h,
                            true, best_area, best_w, best_h,
                            optimize_target)) {
                        best_area = shelf_area;
                        best_w = shelf_w;
                        best_h = shelf_h;
                        best_sprites = std::move(shelf_sprites);
                    }
                }
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
                    if (pick_better_layout_candidate(
                            fast_area, fast_w, fast_h,
                            true, best_area, best_w, best_h,
                            optimize_target)) {
                        best_area = fast_area;
                        best_w = fast_w;
                        best_h = fast_h;
                        best_sprites = std::move(fast_baseline);
                    }
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
    std::cout << "atlas " << atlas_width << "," << atlas_height << "\n";
    std::cout << "scale " << std::setprecision(8) << scale << "\n";
    for (auto& s : sprites) {
        std::string path = s.path;
        size_t pos = 0;
        while ((pos = path.find('"', pos)) != std::string::npos) {
            path.insert(pos, "\\");
            pos += 2;
        }
        std::cout << "sprite \"" << path << "\" "
                  << s.x << "," << s.y << " "
                  << s.w << "," << s.h;
        if (trim_transparent) {
            std::cout << " " << s.trim_left << "," << s.trim_top
                      << " " << s.trim_right << "," << s.trim_bottom;
        }
        std::cout << "\n";
    }

    return 0;
}
