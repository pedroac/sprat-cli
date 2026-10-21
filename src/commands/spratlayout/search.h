// Layout searches: try packers, sort orders and atlas sizes and keep the best layout
// for the optimize target.
#pragma once

#include "commands/spratlayout/common.h"
#include "commands/spratlayout/packing.h"

#include <cstddef>
#include <vector>

namespace sprat::layout {

struct SearchRequest {
    // Largest padded sprite size and the sums of padded sizes.
    int max_sprite_width = 0;
    int max_sprite_height = 0;
    int sum_width = 0;
    int sum_height = 0;
    size_t total_area = 0;  // sum of padded sprite areas
    // Atlas bounds: the user limits (0 = none) and the effective bounds.
    int max_width_limit = 0;
    int max_height_limit = 0;
    int width_upper_bound = 0;
    int height_upper_bound = 0;
    int padding = 0;
    bool allow_rotate = false;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    // The sprites are already in the requested order, which packing must keep.
    bool enforce_sort_order = false;
    // That order is --sort stable with this metric.
    bool enforce_stable_order = false;
    StableMetric stable_metric = StableMetric::Area;
    unsigned int thread_limit = 0;  // 0 = hardware concurrency
    // Layout from a previous run, used as a hint for the atlas width.
    const LayoutSeedCache* seed_hint = nullptr;
};

// Every search prints its own error message and returns false when no layout fits.
bool search_pot_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                       int& out_width, int& out_height);

// The best layouts for each optimize target; both are kept so other profiles can reuse them.
struct CompactSearchResult {
    LayoutCandidate best_gpu;
    LayoutCandidate best_space;
};
bool search_compact_layout(const SearchRequest& request, const std::vector<Sprite>& sprites,
                           CompactSearchResult& out);
// The candidate for the optimize target, or the other one (with a warning) if missing.
const LayoutCandidate* select_compact_candidate(const CompactSearchResult& result,
                                                OptimizeTarget optimize_target);

bool search_fast_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                        int& out_width, int& out_height);

bool search_grid_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                        int& out_width, int& out_height);

// Splits the sprites across as many atlases as needed (sets atlas_index).
bool pack_atlases(std::vector<Sprite>& sprites,
                  int max_w,
                  int max_h,
                  int padding,
                  Mode mode,
                  OptimizeTarget optimize_target,
                  bool allow_rotate,
                  bool enforce_sort_order,
                  std::vector<Atlas>& out_atlases);

} // namespace sprat::layout
