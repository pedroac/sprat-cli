// Packing primitives: place a list of sprites into one bounded atlas.
//
// Each packer sets x, y and rotated on the sprites it places (rotated sprites have
// w and h swapped) and reports the used atlas size. Padding is added to the right and
// bottom of every sprite.
#pragma once

#include "commands/spratlayout/common.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sprat::layout {

enum class RectHeuristic : std::uint8_t {
    BestShortSideFit,
    BestAreaFit,
    BottomLeft
};

enum class SortMode : std::uint8_t {
    Height,
    Width,
    Area,
    MaxSide,
    Perimeter,
    None
};

// The order in which packing searches try sort modes; ties keep the earliest result.
constexpr std::array<SortMode, 6> k_search_sort_modes = {
    SortMode::Area,
    SortMode::MaxSide,
    SortMode::Height,
    SortMode::Width,
    SortMode::Perimeter,
    SortMode::None
};

constexpr std::array<RectHeuristic, 3> k_rect_heuristics = {
    RectHeuristic::BestShortSideFit,
    RectHeuristic::BestAreaFit,
    RectHeuristic::BottomLeft
};

struct LayoutCandidate {
    bool valid = false;
    size_t area = 0;
    int w = 0;
    int h = 0;
    std::vector<Sprite> sprites;
};

bool sort_sprites_by_mode(std::vector<Sprite>& sprites, SortMode mode);
// Deterministic order: by the metric, then by natural path order.
void sort_sprites_stable(std::vector<Sprite>& sprites, StableMetric metric);

// Binary-tree packer into a fixed width x height atlas (used by POT mode).
bool pack_binary_tree(std::vector<Sprite>& sprites, int width, int height, int padding, bool allow_rotate);

// MaxRects packer: width is bounded by width_limit, height by max_height.
bool pack_compact_maxrects(
    std::vector<Sprite>& sprites,
    int width_limit,
    int padding,
    int max_height,
    RectHeuristic heuristic,
    bool allow_rotate,
    int& out_width,
    int& out_height);

struct PartialPackResult {
    std::vector<Sprite> packed;
    std::vector<Sprite> remaining;
    int used_w = 0;
    int used_h = 0;
    size_t packed_area = 0;
};

// MaxRects packer that places what fits and returns the rest (used by multipack).
bool pack_compact_maxrects_partial(
    const std::vector<Sprite>& sprites,
    int width_limit,
    int max_height,
    int padding,
    RectHeuristic heuristic,
    bool allow_rotate,
    PartialPackResult& out);

// Shelf packer: rows of sprites no wider than max_row_width.
bool pack_fast_shelf(
    std::vector<Sprite>& sprites,
    int max_row_width,
    int padding,
    bool allow_rotate,
    int& out_width,
    int& out_height);

// Uniform cells; all sprites must have the same size.
bool pack_grid(
    std::vector<Sprite>& sprites,
    int padding,
    int width_limit,
    int height_limit,
    int& out_width,
    int& out_height);

// Places the sprites exactly where the seed had them. Fails unless every sprite
// matches a seed entry and the result stays within the bounds without overlaps.
bool try_apply_layout_seed(const LayoutSeedCache& seed,
                           int padding,
                           int width_upper_bound,
                           int height_upper_bound,
                           const std::vector<Sprite>& source_sprites,
                           std::vector<Sprite>& out_sprites,
                           int& out_atlas_width,
                           int& out_atlas_height);

// Keeps the seed position of every unchanged sprite and packs the others around them.
bool pack_incremental_maxrects(
    const LayoutSeedCache& seed,
    int padding,
    int width_upper_bound,
    int height_upper_bound,
    const std::vector<Sprite>& source_sprites,
    bool allow_rotate,
    std::vector<Sprite>& out_sprites,
    int& out_atlas_width,
    int& out_atlas_height);

// Smallest bounds that contain every sprite (without trailing padding).
bool compute_tight_atlas_bounds(const std::vector<Sprite>& sprites, int& out_width, int& out_height);

// Returns -1 on overflow.
int next_power_of_two(int v);

// True if the candidate atlas is better than the best one for the optimize target:
// GPU prefers the smaller longest side, SPACE the smaller area.
bool pick_better_layout_candidate(
    size_t candidate_area,
    int candidate_w,
    int candidate_h,
    bool have_best,
    size_t best_area,
    int best_w,
    int best_h,
    OptimizeTarget optimize_target);

} // namespace sprat::layout
