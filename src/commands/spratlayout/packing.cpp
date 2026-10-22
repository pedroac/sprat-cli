#include "commands/spratlayout/packing.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/checked_math.h"
#include "core/cli_parse.h"
#include "core/i18n.h"

namespace sprat::layout {

using sprat::core::checked_add_int;
using sprat::core::checked_mul_size_t;
using sprat::core::tr;

namespace {

struct Node {
    int x, y, w, h;
    bool used = false;
    std::unique_ptr<Node> right;
    std::unique_ptr<Node> down;

    Node(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
};

Node* insert(Node* node, Sprite& sprite, int w, int h, bool allow_rotate, bool* used_rotated = nullptr) {
    if (node->used) {
        if (node->right) {
            if (Node* r = insert(node->right.get(), sprite, w, h, allow_rotate, used_rotated)) {
                return r;
            }
        }
        if (node->down) {
            return insert(node->down.get(), sprite, w, h, allow_rotate, used_rotated);
        }
        return nullptr;
    }

    bool fits_normal = (w <= node->w && h <= node->h);
    bool fits_rotated = allow_rotate && (h <= node->w && w <= node->h);
    if (!fits_normal && !fits_rotated) {
        return nullptr;
    }

    int place_w = w;
    int place_h = h;
    bool rotate = false;
    if (allow_rotate) {
        if (!fits_normal && fits_rotated) {
            place_w = h;
            place_h = w;
            rotate = true;
        } else if (fits_normal && fits_rotated) {
            const int normal_short = std::min(node->w - w, node->h - h);
            const int rotated_short = std::min(node->w - h, node->h - w);
            if (rotated_short < normal_short) {
                place_w = h;
                place_h = w;
                rotate = true;
            }
        }
    }

    if (place_w == node->w && place_h == node->h) {
        node->used = true;
        if (used_rotated != nullptr) {
            *used_rotated = rotate;
        }
        return node;
    }
    node->used = true;
    node->down = std::make_unique<Node>(node->x, node->y + place_h, node->w, node->h - place_h);
    node->right = std::make_unique<Node>(node->x + place_w, node->y, node->w - place_w, place_h);
    if (used_rotated != nullptr) {
        *used_rotated = rotate;
    }
    return node;
}

bool try_pack(std::unique_ptr<Node>& root, std::vector<Sprite>& sprites, int padding = 0, bool allow_rotate = false) {
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
        bool rotated = false;
        Node* node = insert(root.get(), sprite, w, h, allow_rotate, &rotated);
        if (node == nullptr) {
            return false;
        }
        if (rotated) {
            std::swap(sprite.w, sprite.h);
        }
        sprite.rotated = rotated;
        sprite.x = node->x;
        sprite.y = node->y;
    }
    return true;
}

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

bool rects_intersect(const Rect& a, const Rect& b) {
    return a.x + a.w > b.x && b.x + b.w > a.x &&
             a.y + a.h > b.y && b.y + b.h > a.y;
}

bool rect_contains(const Rect& a, const Rect& b) {
    return b.x >= a.x && b.y >= a.y &&
           b.x + b.w <= a.x + a.w &&
           b.y + b.h <= a.y + a.h;
}

void append_pruned_free_rect(std::vector<Rect>& out, const Rect& candidate) {
    if (candidate.w <= 0 || candidate.h <= 0) {
        return;
    }

    for (const Rect& existing : out) {
        if (rect_contains(existing, candidate)) {
            return;
        }
    }

    size_t write = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (!rect_contains(candidate, out[i])) {
            if (write != i) {
                out[write] = out[i];
            }
            ++write;
        }
    }
    out.resize(write);
    out.push_back(candidate);
}

bool split_free_rect(const Rect& free_rect, const Rect& used_rect, std::vector<Rect>& out) {
    if (!rects_intersect(free_rect, used_rect)) {
        append_pruned_free_rect(out, free_rect);
        return true;
    }

    int free_right = free_rect.x + free_rect.w;
    int free_bottom = free_rect.y + free_rect.h;
    int used_right = used_rect.x + used_rect.w;
    int used_bottom = used_rect.y + used_rect.h;

    if (used_rect.x > free_rect.x) {
        append_pruned_free_rect(out, {free_rect.x, free_rect.y, used_rect.x - free_rect.x, free_rect.h});
    }
    if (used_right < free_right) {
        append_pruned_free_rect(out, {used_right, free_rect.y, free_right - used_right, free_rect.h});
    }
    if (used_rect.y > free_rect.y) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            append_pruned_free_rect(out, {x0, free_rect.y, x1 - x0, used_rect.y - free_rect.y});
        }
    }
    if (used_bottom < free_bottom) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            append_pruned_free_rect(out, {x0, used_bottom, x1 - x0, free_bottom - used_bottom});
        }
    }
    return true;
}

} // namespace

bool pack_binary_tree(std::vector<Sprite>& sprites, int width, int height, int padding, bool allow_rotate) {
    auto root = std::make_unique<Node>(0, 0, width, height);
    return try_pack(root, sprites, padding, allow_rotate);
}

bool sort_sprites_by_mode(std::vector<Sprite>& sprites, SortMode mode) {
    auto cmp_height = [](const Sprite& a, const Sprite& b) {
        if (a.h != b.h) { return a.h > b.h;
}
        return a.w > b.w;
    };
    auto cmp_width = [](const Sprite& a, const Sprite& b) {
        if (a.w != b.w) { return a.w > b.w;
}
        return a.h > b.h;
    };
    auto cmp_area = [](const Sprite& a, const Sprite& b) {
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        if (area_a != area_b) { return area_a > area_b;
}
        if (a.h != b.h) { return a.h > b.h;
}
        return a.w > b.w;
    };
    auto cmp_max_side = [](const Sprite& a, const Sprite& b) {
        int max_a = std::max(a.w, a.h);
        int max_b = std::max(b.w, b.h);
        if (max_a != max_b) { return max_a > max_b;
}
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };
    auto cmp_perimeter = [](const Sprite& a, const Sprite& b) {
        int p_a = a.w + a.h;
        int p_b = b.w + b.h;
        if (p_a != p_b) { return p_a > p_b;
}
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };

    switch (mode) {
        case SortMode::Height:
            std::ranges::sort(sprites, cmp_height);
            return true;
        case SortMode::Width:
            std::ranges::sort(sprites, cmp_width);
            return true;
        case SortMode::Area:
            std::ranges::sort(sprites, cmp_area);
            return true;
        case SortMode::MaxSide:
            std::ranges::sort(sprites, cmp_max_side);
            return true;
        case SortMode::Perimeter:
            std::ranges::sort(sprites, cmp_perimeter);
            return true;
        case SortMode::None:
            return true;
    }
    return false;
}

void sort_sprites_stable(std::vector<Sprite>& sprites, StableMetric metric) {
    auto path_cmp = [](const Sprite& a, const Sprite& b) {
        return sprat::core::compare_natural(a.path, b.path) < 0;
    };
    switch (metric) {
        case StableMetric::Area:
            std::ranges::sort(sprites, [&](const Sprite& a, const Sprite& b) {
                const long long area_a = static_cast<long long>(a.w) * a.h;
                const long long area_b = static_cast<long long>(b.w) * b.h;
                if (area_a != area_b) { return area_a > area_b; }
                if (a.h != b.h) { return a.h > b.h; }
                if (a.w != b.w) { return a.w > b.w; }
                return path_cmp(a, b);
            });
            break;
        case StableMetric::MaxSide:
            std::ranges::sort(sprites, [&](const Sprite& a, const Sprite& b) {
                const int max_a = std::max(a.w, a.h);
                const int max_b = std::max(b.w, b.h);
                if (max_a != max_b) { return max_a > max_b; }
                const long long area_a = static_cast<long long>(a.w) * a.h;
                const long long area_b = static_cast<long long>(b.w) * b.h;
                if (area_a != area_b) { return area_a > area_b; }
                return path_cmp(a, b);
            });
            break;
        case StableMetric::Height:
            std::ranges::sort(sprites, [&](const Sprite& a, const Sprite& b) {
                if (a.h != b.h) { return a.h > b.h; }
                if (a.w != b.w) { return a.w > b.w; }
                return path_cmp(a, b);
            });
            break;
        case StableMetric::Width:
            std::ranges::sort(sprites, [&](const Sprite& a, const Sprite& b) {
                if (a.w != b.w) { return a.w > b.w; }
                if (a.h != b.h) { return a.h > b.h; }
                return path_cmp(a, b);
            });
            break;
        case StableMetric::Perimeter:
            std::ranges::sort(sprites, [&](const Sprite& a, const Sprite& b) {
                const int p_a = a.w + a.h;
                const int p_b = b.w + b.h;
                if (p_a != p_b) { return p_a > p_b; }
                const long long area_a = static_cast<long long>(a.w) * a.h;
                const long long area_b = static_cast<long long>(b.w) * b.h;
                if (area_a != area_b) { return area_a > area_b; }
                return path_cmp(a, b);
            });
            break;
    }
}

bool pack_compact_maxrects(
    std::vector<Sprite>& sprites,
    int width_limit,
    int padding,
    int max_height,
    RectHeuristic heuristic,
    bool allow_rotate,
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
    std::vector<Rect> next_free;

    for (auto& s : sprites) {
        int rw = 0;
        int rh = 0;
        if (!checked_add_int(s.w, padding, rw) || !checked_add_int(s.h, padding, rh)) {
            return false;
        }
        int rrw = rh;
        int rrh = rw;
        if (rw <= 0 || rh <= 0 || rw > width_limit || rh > max_height) {
            if (!(allow_rotate && rrw > 0 && rrh > 0 && rrw <= width_limit && rrh <= max_height)) {
                return false;
            }
        }

        int best_index = -1;
        int best_short_fit = std::numeric_limits<int>::max();
        int best_long_fit = std::numeric_limits<int>::max();
        int best_area_fit = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_left = std::numeric_limits<int>::max();
        bool best_rotated = false;

        for (size_t i = 0; i < free_rects.size(); ++i) {
            const Rect& fr = free_rects[i];
            auto consider_orientation = [&](int cand_w, int cand_h, bool rotated) {
                if (cand_w > fr.w || cand_h > fr.h) {
                    return;
                }
                int leftover_h = fr.h - cand_h;
                int leftover_w = fr.w - cand_w;
                int short_fit = std::min(leftover_h, leftover_w);
                int long_fit = std::max(leftover_h, leftover_w);
                int area_fit = leftover_h * leftover_w;

                bool better = false;
                switch (heuristic) {
                    case RectHeuristic::BestShortSideFit:
                        better = short_fit < best_short_fit ||
                                 (short_fit == best_short_fit && long_fit < best_long_fit) ||
                                 (short_fit == best_short_fit && long_fit == best_long_fit && fr.y < best_top) ||
                                 (short_fit == best_short_fit && long_fit == best_long_fit && fr.y == best_top && fr.x < best_left);
                        break;
                    case RectHeuristic::BestAreaFit:
                        better = area_fit < best_area_fit ||
                                 (area_fit == best_area_fit && short_fit < best_short_fit) ||
                                 (area_fit == best_area_fit && short_fit == best_short_fit && fr.y < best_top) ||
                                 (area_fit == best_area_fit && short_fit == best_short_fit && fr.y == best_top && fr.x < best_left);
                        break;
                    case RectHeuristic::BottomLeft:
                        better = fr.y < best_top || (fr.y == best_top && fr.x < best_left) ||
                                 (fr.y == best_top && fr.x == best_left && short_fit < best_short_fit);
                        break;
                }

                if (!better) {
                    return;
                }
                best_index = static_cast<int>(i);
                best_short_fit = short_fit;
                best_long_fit = long_fit;
                best_area_fit = area_fit;
                best_top = fr.y;
                best_left = fr.x;
                best_rotated = rotated;
            };

            consider_orientation(rw, rh, false);
            if (allow_rotate && s.w != s.h) {
                consider_orientation(rrw, rrh, true);
            }
        }

        if (best_index < 0) {
            return false;
        }

        int used_w_dim = rw;
        int used_h_dim = rh;
        if (best_rotated) {
            std::swap(s.w, s.h);
            std::swap(used_w_dim, used_h_dim);
        }
        s.rotated = best_rotated;

        Rect used = {.x=free_rects[static_cast<size_t>(best_index)].x,
                     .y=free_rects[static_cast<size_t>(best_index)].y,
                     .w=used_w_dim, .h=used_h_dim};
        s.x = used.x;
        s.y = used.y;

        used_w = std::max(used.x + used.w, used_w);
        used_h = std::max(used.y + used.h, used_h);

        next_free.clear();
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }

        std::swap(free_rects, next_free);
    }

    out_width = used_w;
    out_height = used_h;
    return out_width > 0 && out_height > 0;
}

bool pack_incremental_maxrects(
    const LayoutSeedCache& seed,
    int padding,
    int width_upper_bound,
    int height_upper_bound,
    const std::vector<Sprite>& source_sprites,
    bool allow_rotate,
    std::vector<Sprite>& out_sprites,
    int& out_atlas_width,
    int& out_atlas_height) {

    // Build lookup of seed entries by path
    std::unordered_map<std::string, const LayoutSeedEntry*> seed_by_path;
    seed_by_path.reserve(seed.entries.size());
    for (const auto& entry : seed.entries) {
        seed_by_path.emplace(entry.path, &entry);
    }

    // Classify each sprite as pinned or new
    std::vector<Sprite> pinned_sprites;
    std::vector<Sprite> new_sprites;
    pinned_sprites.reserve(source_sprites.size());
    new_sprites.reserve(source_sprites.size());

    for (const auto& src : source_sprites) {
        auto it = seed_by_path.find(src.path);
        if (it != seed_by_path.end()) {
            const LayoutSeedEntry& entry = *it->second;
            const int expected_w = entry.rotated ? src.h : src.w;
            const int expected_h = entry.rotated ? src.w : src.h;
            if (entry.x >= 0 && entry.y >= 0 &&
                entry.w == expected_w && entry.h == expected_h &&
                entry.trim_left == src.trim_left && entry.trim_top == src.trim_top &&
                entry.trim_right == src.trim_right && entry.trim_bottom == src.trim_bottom) {
                Sprite placed = src;
                placed.x = entry.x;
                placed.y = entry.y;
                placed.rotated = entry.rotated;
                if (placed.rotated) {
                    std::swap(placed.w, placed.h);
                }
                pinned_sprites.push_back(std::move(placed));
                continue;
            }
        }
        new_sprites.push_back(src);
    }

    // No benefit from incremental if nothing is pinned
    if (pinned_sprites.empty()) {
        return false;
    }

    // Validate pinned sprites fit within bounds and don't overlap (sweep-line check)
    {
        struct PinnedRect {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
        };
        std::vector<PinnedRect> prects;
        prects.reserve(pinned_sprites.size());
        for (const auto& s : pinned_sprites) {
            int padded_w = 0;
            int padded_h = 0;
            int x1 = 0;
            int y1 = 0;
            if (!checked_add_int(s.w, padding, padded_w) ||
                !checked_add_int(s.h, padding, padded_h) ||
                !checked_add_int(s.x, padded_w, x1) ||
                !checked_add_int(s.y, padded_h, y1)) {
                return false;
            }
            if (padded_w <= 0 || padded_h <= 0 || x1 > width_upper_bound || y1 > height_upper_bound) {
                return false;
            }
            prects.push_back({.x0=s.x, .y0=s.y, .x1=x1, .y1=y1});
        }

        std::vector<size_t> order(prects.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::ranges::sort(order, [&](size_t a, size_t b) {
            return prects[a].x0 < prects[b].x0;
        });
        for (size_t i = 0; i < order.size(); ++i) {
            const PinnedRect& a = prects[order[i]];
            for (size_t j = i + 1; j < order.size(); ++j) {
                const PinnedRect& b = prects[order[j]];
                if (b.x0 >= a.x1) {
                    break;
                }
                if (a.y0 < b.y1 && b.y0 < a.y1) {
                    return false;
                }
            }
        }
    }

    // Build MaxRects free_rects: start with full area, split around each pinned sprite
    std::vector<Rect> free_rects;
    free_rects.push_back({0, 0, width_upper_bound, height_upper_bound});

    for (const auto& s : pinned_sprites) {
        int padded_w = 0;
        int padded_h = 0;
        checked_add_int(s.w, padding, padded_w);
        checked_add_int(s.h, padding, padded_h);
        Rect used = {.x=s.x, .y=s.y, .w=padded_w, .h=padded_h};

        std::vector<Rect> next_free;
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }
        std::swap(free_rects, next_free);
    }

    // Sort new sprites by area descending
    std::ranges::sort(new_sprites, [](const Sprite& a, const Sprite& b) {
        return (a.w * a.h) > (b.w * b.h);
    });

    // Place each new sprite using BestShortSideFit heuristic
    for (auto& s : new_sprites) {
        int rw = 0;
        int rh = 0;
        if (!checked_add_int(s.w, padding, rw) || !checked_add_int(s.h, padding, rh)) {
            return false;
        }
        int rrw = rh;
        int rrh = rw;
        if (rw <= 0 || rh <= 0 || rw > width_upper_bound || rh > height_upper_bound) {
            if (!(allow_rotate && rrw > 0 && rrh > 0 && rrw <= width_upper_bound && rrh <= height_upper_bound)) {
                return false;
            }
        }

        int best_index = -1;
        int best_short_fit = std::numeric_limits<int>::max();
        int best_long_fit = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_left = std::numeric_limits<int>::max();
        bool best_rotated = false;

        for (size_t i = 0; i < free_rects.size(); ++i) {
            const Rect& fr = free_rects[i];
            auto consider_orientation = [&](int cand_w, int cand_h, bool rotated) {
                if (cand_w > fr.w || cand_h > fr.h) {
                    return;
                }
                int leftover_h = fr.h - cand_h;
                int leftover_w = fr.w - cand_w;
                int short_fit = std::min(leftover_h, leftover_w);
                int long_fit = std::max(leftover_h, leftover_w);

                bool better = short_fit < best_short_fit ||
                              (short_fit == best_short_fit && long_fit < best_long_fit) ||
                              (short_fit == best_short_fit && long_fit == best_long_fit && fr.y < best_top) ||
                              (short_fit == best_short_fit && long_fit == best_long_fit && fr.y == best_top && fr.x < best_left);

                if (!better) {
                    return;
                }
                best_index = static_cast<int>(i);
                best_short_fit = short_fit;
                best_long_fit = long_fit;
                best_top = fr.y;
                best_left = fr.x;
                best_rotated = rotated;
            };

            consider_orientation(rw, rh, false);
            if (allow_rotate && s.w != s.h) {
                consider_orientation(rrw, rrh, true);
            }
        }

        if (best_index < 0) {
            return false;
        }

        int used_w_dim = rw;
        int used_h_dim = rh;
        if (best_rotated) {
            std::swap(s.w, s.h);
            std::swap(used_w_dim, used_h_dim);
        }
        s.rotated = best_rotated;

        Rect used = {.x=free_rects[static_cast<size_t>(best_index)].x,
                     .y=free_rects[static_cast<size_t>(best_index)].y,
                     .w=used_w_dim, .h=used_h_dim};
        s.x = used.x;
        s.y = used.y;

        std::vector<Rect> next_free;
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }
        std::swap(free_rects, next_free);
    }

    // Combine pinned + newly placed sprites
    out_sprites.clear();
    out_sprites.reserve(pinned_sprites.size() + new_sprites.size());
    for (auto& s : pinned_sprites) {
        out_sprites.push_back(std::move(s));
    }
    for (auto& s : new_sprites) {
        out_sprites.push_back(std::move(s));
    }

    // Compute tight atlas bounds
    out_atlas_width = 0;
    out_atlas_height = 0;
    for (const auto& s : out_sprites) {
        int padded_w = 0;
        int padded_h = 0;
        checked_add_int(s.w, padding, padded_w);
        checked_add_int(s.h, padding, padded_h);
        out_atlas_width = std::max(out_atlas_width, s.x + padded_w);
        out_atlas_height = std::max(out_atlas_height, s.y + padded_h);
    }

    return out_atlas_width > 0 && out_atlas_height > 0;
}

bool pack_compact_maxrects_partial(
    const std::vector<Sprite>& sprites,
    int width_limit,
    int max_height,
    int padding,
    RectHeuristic heuristic,
    bool allow_rotate,
    PartialPackResult& out
) {
    out = {};
    if (width_limit <= 0 || max_height <= 0) {
        return false;
    }

    std::vector<Rect> free_rects;
    free_rects.push_back({0, 0, width_limit, max_height});
    std::vector<Rect> next_free;

    for (const auto& src : sprites) {
        Sprite s = src;
        int rw = 0;
        int rh = 0;
        if (!checked_add_int(s.w, padding, rw) || !checked_add_int(s.h, padding, rh)) {
            return false;
        }
        const int rrw = rh;
        const int rrh = rw;
        if (rw <= 0 || rh <= 0) {
            out.remaining.push_back(src);
            continue;
        }

        int best_index = -1;
        int best_short_fit = std::numeric_limits<int>::max();
        int best_long_fit = std::numeric_limits<int>::max();
        int best_area_fit = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_left = std::numeric_limits<int>::max();
        bool best_rotated = false;

        for (size_t i = 0; i < free_rects.size(); ++i) {
            const Rect& fr = free_rects[i];
            auto consider_orientation = [&](int cand_w, int cand_h, bool rotated) {
                if (cand_w <= 0 || cand_h <= 0 || cand_w > fr.w || cand_h > fr.h) {
                    return;
                }
                int leftover_h = fr.h - cand_h;
                int leftover_w = fr.w - cand_w;
                int short_fit = std::min(leftover_h, leftover_w);
                int long_fit = std::max(leftover_h, leftover_w);
                int area_fit = leftover_h * leftover_w;

                bool better = false;
                switch (heuristic) {
                    case RectHeuristic::BestShortSideFit:
                        better = short_fit < best_short_fit ||
                                 (short_fit == best_short_fit && long_fit < best_long_fit) ||
                                 (short_fit == best_short_fit && long_fit == best_long_fit && fr.y < best_top) ||
                                 (short_fit == best_short_fit && long_fit == best_long_fit && fr.y == best_top && fr.x < best_left);
                        break;
                    case RectHeuristic::BestAreaFit:
                        better = area_fit < best_area_fit ||
                                 (area_fit == best_area_fit && short_fit < best_short_fit) ||
                                 (area_fit == best_area_fit && short_fit == best_short_fit && fr.y < best_top) ||
                                 (area_fit == best_area_fit && short_fit == best_short_fit && fr.y == best_top && fr.x < best_left);
                        break;
                    case RectHeuristic::BottomLeft:
                        better = fr.y < best_top || (fr.y == best_top && fr.x < best_left) ||
                                 (fr.y == best_top && fr.x == best_left && short_fit < best_short_fit);
                        break;
                }
                if (!better) {
                    return;
                }
                best_index = static_cast<int>(i);
                best_short_fit = short_fit;
                best_long_fit = long_fit;
                best_area_fit = area_fit;
                best_top = fr.y;
                best_left = fr.x;
                best_rotated = rotated;
            };

            consider_orientation(rw, rh, false);
            if (allow_rotate && s.w != s.h) {
                consider_orientation(rrw, rrh, true);
            }
        }

        if (best_index < 0) {
            out.remaining.push_back(src);
            continue;
        }

        int used_w_dim = rw;
        int used_h_dim = rh;
        if (best_rotated) {
            std::swap(s.w, s.h);
            std::swap(used_w_dim, used_h_dim);
        }
        s.rotated = best_rotated;

        Rect used = {.x=free_rects[static_cast<size_t>(best_index)].x,
                     .y=free_rects[static_cast<size_t>(best_index)].y,
                     .w=used_w_dim, .h=used_h_dim};
        s.x = used.x;
        s.y = used.y;

        out.used_w = std::max(out.used_w, used.x + used.w);
        out.used_h = std::max(out.used_h, used.y + used.h);

        size_t sprite_area = 0;
        if (!checked_mul_size_t(static_cast<size_t>(s.w), static_cast<size_t>(s.h), sprite_area) ||
            sprite_area > std::numeric_limits<size_t>::max() - out.packed_area) {
            return false;
        }
        out.packed_area += sprite_area;

        next_free.clear();
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }
        std::swap(free_rects, next_free);
        out.packed.push_back(s);
    }

    return !out.packed.empty() && out.used_w > 0 && out.used_h > 0;
}

bool pack_fast_shelf(
    std::vector<Sprite>& sprites,
    int max_row_width,
    int padding,
    bool allow_rotate,
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
        if (!checked_add_int(s.w, padding, w) || !checked_add_int(s.h, padding, h)) {
            return false;
        }

        auto score_orientation = [&](int /*place_w*/, int place_h, bool new_row_needed) -> long long {
            const int row_h = new_row_needed ? place_h : std::max(row_height, place_h);
            const int row_y = new_row_needed ? (y + row_height) : y;
            return (static_cast<long long>(row_y) << 32) + static_cast<long long>(row_h);
        };

        const bool can_rotate = allow_rotate && s.w != s.h;
        int best_place_w = w;
        int best_place_h = h;
        bool best_rotated = false;

        auto choose_better = [&](int cand_w, int cand_h, bool cand_rotated) {
            if (cand_w <= 0 || cand_h <= 0 || cand_w > max_row_width) {
                return;
            }
            const bool cand_new_row = (x > 0 && x + cand_w > max_row_width);
            if (cand_new_row && cand_w > max_row_width) {
                return;
            }
            const bool best_new_row = (x > 0 && x + best_place_w > max_row_width);
            const long long cand_score = score_orientation(cand_w, cand_h, cand_new_row);
            const long long best_score = score_orientation(best_place_w, best_place_h, best_new_row);
            if (cand_score < best_score) {
                best_place_w = cand_w;
                best_place_h = cand_h;
                best_rotated = cand_rotated;
            }
        };

        if (can_rotate) {
            choose_better(h, w, true);
        }

        int candidate_x = x + best_place_w;
        int next_y = 0;
        if (x > 0 && candidate_x > max_row_width) {
            if (!checked_add_int(y, row_height, next_y)) {
                return false;
            }
            y = next_y;
            x = 0;
            row_height = 0;
            candidate_x = best_place_w;
        }
        if (candidate_x > max_row_width) {
            return false;
        }
        if (best_rotated) {
            std::swap(s.w, s.h);
        }
        s.rotated = best_rotated;

        s.x = x;
        s.y = y;
        x = candidate_x;
        row_height = std::max(best_place_h, row_height);
        atlas_width = std::max(x, atlas_width);
    }

    int total_height = 0;
    if (!checked_add_int(y, row_height, total_height)) {
        return false;
    }
    out_width = atlas_width;
    out_height = total_height;
    return out_width > 0 && out_height > 0;
}

// Pack sprites into a uniform grid where every cell is max_sprite_width x max_sprite_height.
// Sprites are placed left-to-right, top-to-bottom.  The number of columns is chosen to make
// the atlas as square as possible, subject to width_limit / height_limit (0 = no limit).
bool pack_grid(
    std::vector<Sprite>& sprites,
    int padding,
    int width_limit,
    int height_limit,
    int& out_width,
    int& out_height
) {
    if (sprites.empty()) {
        return false;
    }

    int cell_w = 0;
    int cell_h = 0;
    if (!grid_cell_size(sprites, padding, cell_w, cell_h)) {
        return false;
    }
    if ((width_limit > 0 && cell_w > width_limit) || (height_limit > 0 && cell_h > height_limit)) {
        return false;
    }

    int n = static_cast<int>(sprites.size());
    int max_cols = (width_limit > 0) ? (width_limit / cell_w) : n;
    if (max_cols <= 0) {
        return false;
    }

    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (cols <= 0) {
        cols = 1;
    }
    cols = std::min(cols, max_cols);

    // Widen if height limit is exceeded
    if (height_limit > 0) {
        while (cols < max_cols) {
            int rows_needed = (n + cols - 1) / cols;
            if (static_cast<long long>(rows_needed) * cell_h <= static_cast<long long>(height_limit)) {
                break;
            }
            ++cols;
        }
        int rows_needed = (n + cols - 1) / cols;
        if (static_cast<long long>(rows_needed) * cell_h > static_cast<long long>(height_limit)) {
            return false;
        }
    }

    for (int i = 0; i < n; ++i) {
        int col = i % cols;
        int row = i / cols;
        long long x_ll = static_cast<long long>(col) * cell_w;
        long long y_ll = static_cast<long long>(row) * cell_h;
        if (x_ll > std::numeric_limits<int>::max() || y_ll > std::numeric_limits<int>::max()) {
            return false;
        }
        sprites[i].x = static_cast<int>(x_ll);
        sprites[i].y = static_cast<int>(y_ll);
    }

    int rows = (n + cols - 1) / cols;
    long long total_w_ll = static_cast<long long>(cols) * cell_w;
    long long total_h_ll = static_cast<long long>(rows) * cell_h;
    if (total_w_ll > std::numeric_limits<int>::max() || total_h_ll > std::numeric_limits<int>::max()) {
        return false;
    }
    out_width = static_cast<int>(total_w_ll);
    out_height = static_cast<int>(total_h_ll);
    return true;
}

bool grid_cell_size(const std::vector<Sprite>& sprites, int padding, int& cell_w, int& cell_h) {
    if (sprites.empty()) {
        return false;
    }
    const int ref_w = sprites[0].w;
    const int ref_h = sprites[0].h;
    for (const auto& s : sprites) {
        if (s.w != ref_w || s.h != ref_h) {
            std::cerr << tr("Error: grid mode requires all sprites to be the same size; ")
                      << s.path << " is " << s.w << "x" << s.h
                      << tr(", expected ") << ref_w << "x" << ref_h << '\n';
            return false;
        }
    }
    if (!checked_add_int(ref_w, padding, cell_w) || !checked_add_int(ref_h, padding, cell_h)) {
        return false;
    }
    return cell_w > 0 && cell_h > 0;
}

bool compute_tight_atlas_bounds(const std::vector<Sprite>& sprites, int& out_width, int& out_height) {
    out_width = 0;
    out_height = 0;
    for (const auto& s : sprites) {
        int x1 = 0;
        int y1 = 0;
        if (!checked_add_int(s.x, s.w, x1) || !checked_add_int(s.y, s.h, y1)) {
            return false;
        }
        out_width = std::max(x1, out_width);
        out_height = std::max(y1, out_height);
    }
    return out_width > 0 && out_height > 0;
}

int previous_power_of_two(int v) {
    if (v < 1) {
        return 0;
    }
    int p = 1;
    while (p <= v / 2) {
        p <<= 1;
    }
    return p;
}

int pot_limit(int limit) {
    return limit > 0 ? previous_power_of_two(limit) : 0;
}

int pot_search_bound(int sum, int limit) {
    const int up = next_power_of_two(sum);
    const int cap = limit > 0 ? pot_limit(limit) : up;
    return up > 0 ? std::min(up, cap) : cap;
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
        if (candidate_area != best_area) {
            return candidate_area < best_area;
        }
        if (candidate_aspect_delta != best_aspect_delta) {
            return candidate_aspect_delta < best_aspect_delta;
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

bool try_apply_layout_seed(const LayoutSeedCache& seed,
                           int padding,
                           int width_upper_bound,
                           int height_upper_bound,
                           const std::vector<Sprite>& source_sprites,
                           std::vector<Sprite>& out_sprites,
                           int& out_atlas_width,
                           int& out_atlas_height) {
    if (seed.entries.size() != source_sprites.size()) {
        return false;
    }

    std::unordered_map<std::string, const LayoutSeedEntry*> seed_by_path;
    seed_by_path.reserve(seed.entries.size());
    for (const auto& entry : seed.entries) {
        auto inserted = seed_by_path.emplace(entry.path, &entry);
        if (!inserted.second) {
            return false;
        }
    }

    std::unordered_set<std::string> seen_paths;
    seen_paths.reserve(source_sprites.size());

    struct Rect {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
    };
    std::vector<Rect> rects;
    rects.reserve(source_sprites.size());

    out_sprites.clear();
    out_sprites.reserve(source_sprites.size());
    out_atlas_width = 0;
    out_atlas_height = 0;

    for (const auto& src : source_sprites) {
        if (!seen_paths.emplace(src.path).second) {
            return false;
        }
        auto it = seed_by_path.find(src.path);
        if (it == seed_by_path.end()) {
            return false;
        }
        const LayoutSeedEntry& entry = *it->second;
        const int expected_w = entry.rotated ? src.h : src.w;
        const int expected_h = entry.rotated ? src.w : src.h;
        if (entry.x < 0 || entry.y < 0 ||
            entry.w != expected_w || entry.h != expected_h ||
            entry.trim_left != src.trim_left || entry.trim_top != src.trim_top ||
            entry.trim_right != src.trim_right || entry.trim_bottom != src.trim_bottom) {
            return false;
        }

        int padded_w = 0;
        int padded_h = 0;
        int x1 = 0;
        int y1 = 0;
        if (!checked_add_int(src.w, padding, padded_w) ||
            !checked_add_int(src.h, padding, padded_h) ||
            !checked_add_int(entry.x, padded_w, x1) ||
            !checked_add_int(entry.y, padded_h, y1)) {
            return false;
        }
        if (padded_w <= 0 || padded_h <= 0 || x1 > width_upper_bound || y1 > height_upper_bound) {
            return false;
        }

        Sprite placed = src;
        placed.x = entry.x;
        placed.y = entry.y;
        placed.rotated = entry.rotated;
        if (placed.rotated) {
            std::swap(placed.w, placed.h);
        }
        out_sprites.push_back(std::move(placed));
        rects.push_back({.x0=entry.x, .y0=entry.y, .x1=x1, .y1=y1});
        out_atlas_width = std::max(out_atlas_width, x1);
        out_atlas_height = std::max(out_atlas_height, y1);
    }
    if (out_atlas_width <= 0 || out_atlas_height <= 0) {
        return false;
    }

    std::vector<size_t> order(rects.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](size_t a, size_t b) {
        return rects[a].x0 < rects[b].x0;
    });

    for (size_t i = 0; i < order.size(); ++i) {
        const Rect& a = rects[order[i]];
        for (size_t j = i + 1; j < order.size(); ++j) {
            const Rect& b = rects[order[j]];
            if (b.x0 >= a.x1) {
                break;
            }
            if (a.y0 < b.y1 && b.y0 < a.y1) {
                return false;
            }
        }
    }

    return true;
}

} // namespace sprat::layout
