#include "commands/spratlayout/search.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <unordered_set>
#include <utility>

#include "core/checked_math.h"
#include "core/i18n.h"
#include "core/parallel.h"

namespace sprat::layout {

using sprat::core::checked_add_int;
using sprat::core::checked_mul_size_t;
using sprat::core::tr;

namespace {

constexpr int k_search_step_divisor = 24;
constexpr int k_search_step_min = 8;
// Multiples of the search step probed around each anchor width.
constexpr std::array<int, 15> k_guided_search_offsets = {
    0, -1, 1, -2, 2, -4, 4, -8, 8, -12, 12, -16, 16, -20, 20
};

// Positions in k_search_sort_modes.
constexpr size_t k_sort_mode_index_area = 0;
constexpr size_t k_sort_mode_index_maxside = 1;
constexpr size_t k_sort_mode_index_height = 2;
constexpr size_t k_sort_mode_index_none = 5;
static_assert(k_search_sort_modes[k_sort_mode_index_area] == SortMode::Area);
static_assert(k_search_sort_modes[k_sort_mode_index_maxside] == SortMode::MaxSide);
static_assert(k_search_sort_modes[k_sort_mode_index_height] == SortMode::Height);
static_assert(k_search_sort_modes[k_sort_mode_index_none] == SortMode::None);

// The sort modes the guided compact search tries at every width.
constexpr std::array<size_t, 4> k_guided_sort_indices = {
    k_sort_mode_index_height,
    k_sort_mode_index_area,
    k_sort_mode_index_maxside,
    k_sort_mode_index_none
};

using SortedSprites = std::array<std::vector<Sprite>, k_search_sort_modes.size()>;

// A requested order allows only SortMode::None.
bool skip_sort_mode(bool enforce_sort_order, SortMode mode) {
    return enforce_sort_order && mode != SortMode::None;
}

// One copy of the sprites per entry of k_search_sort_modes. Each copy is sorted
// starting from the first (area) order, so equal sprites break ties the same way
// in every run.
SortedSprites sort_for_search(const std::vector<Sprite>& sprites, bool enforce_sort_order) {
    SortedSprites sorted;
    sorted[0] = sprites;
    if (!skip_sort_mode(enforce_sort_order, k_search_sort_modes[0])) {
        sort_sprites_by_mode(sorted[0], k_search_sort_modes[0]);
    }
    for (size_t si = 1; si < k_search_sort_modes.size(); ++si) {
        sorted[si] = sorted[0];
        if (!skip_sort_mode(enforce_sort_order, k_search_sort_modes[si])) {
            sort_sprites_by_mode(sorted[si], k_search_sort_modes[si]);
        }
    }
    return sorted;
}

// ceil(sqrt(area)); false when it does not fit an int.
bool ceil_sqrt(size_t area, int& out) {
    const long double root = std::sqrt(static_cast<long double>(area));
    if (root > static_cast<long double>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(std::ceil(root));
    return true;
}

// Width of the seed layout, grown by any padding added since it was made.
int seed_hint_width(const LayoutSeedCache& seed, int padding) {
    int width = seed.atlas_width;
    if (padding > seed.padding) {
        int expanded = 0;
        if (checked_add_int(width, padding - seed.padding, expanded)) {
            width = expanded;
        }
    }
    return width;
}

LayoutCandidate make_candidate(int w, int h, std::vector<Sprite>&& sprites) {
    LayoutCandidate candidate;
    candidate.valid = true;
    candidate.area = static_cast<size_t>(w) * static_cast<size_t>(h);
    candidate.w = w;
    candidate.h = h;
    candidate.sprites = std::move(sprites);
    return candidate;
}

// The best candidate so far for each optimize target.
struct BestCandidates {
    LayoutCandidate gpu;
    LayoutCandidate space;

    void consider(LayoutCandidate&& candidate) {
        if (!candidate.valid || candidate.w <= 0 || candidate.h <= 0) {
            return;
        }
        const bool better_gpu = is_better(candidate, gpu, OptimizeTarget::GPU);
        const bool better_space = is_better(candidate, space, OptimizeTarget::SPACE);
        if (better_gpu && better_space) {
            gpu = std::move(candidate);
            space = gpu;
        } else if (better_gpu) {
            gpu = std::move(candidate);
        } else if (better_space) {
            space = std::move(candidate);
        }
    }

    static bool is_better(const LayoutCandidate& candidate, const LayoutCandidate& best, OptimizeTarget target) {
        return !best.valid ||
            pick_better_layout_candidate(
                candidate.area, candidate.w, candidate.h, true,
                best.area, best.w, best.h,
                target);
    }
};

// The better of a worker's best and the shared best, or nullptr if neither is valid.
const LayoutCandidate* select_better_candidate(const LayoutCandidate& local_best,
                                               const LayoutCandidate& shared_best,
                                               OptimizeTarget target) {
    if (!local_best.valid) {
        return shared_best.valid ? &shared_best : nullptr;
    }
    if (!shared_best.valid) {
        return &local_best;
    }
    if (pick_better_layout_candidate(
            local_best.area, local_best.w, local_best.h, true,
            shared_best.area, shared_best.w, shared_best.h,
            target)) {
        return &local_best;
    }
    return &shared_best;
}

} // namespace

bool pack_atlases(
    std::vector<Sprite>& sprites,
    int max_w,
    int max_h,
    int padding,
    Mode mode,
    OptimizeTarget optimize_target,
    bool allow_rotate,
    bool enforce_sort_order,
    std::vector<Atlas>& out_atlases
) {
    if (sprites.empty()) {
        return true;
    }

    // In pot mode max_w/max_h are pot_search_bound values: POT atlases are rounded up to a
    // power of two after packing, so they must already be powers of two within the limits.

    // Grid mode: uniform-cell layout, split into equal-capacity atlases when needed.
    if (mode == Mode::GRID) {
        int cell_w = 0;
        int cell_h = 0;
        if (!grid_cell_size(sprites, padding, cell_w, cell_h)) {
            return false;
        }
        if (cell_w > max_w || cell_h > max_h) {
            return false;
        }
        int cols = max_w / cell_w;
        int rows_per_atlas = max_h / cell_h;
        if (cols <= 0 || rows_per_atlas <= 0) {
            return false;
        }
        int capacity = cols * rows_per_atlas;
        int n = static_cast<int>(sprites.size());
        int atlas_idx = 0;
        for (int base = 0; base < n; base += capacity, ++atlas_idx) {
            int count = std::min(capacity, n - base);
            int atlas_rows = (count + cols - 1) / cols;
            long long aw = static_cast<long long>(cols) * cell_w;
            long long ah = static_cast<long long>(atlas_rows) * cell_h;
            if (aw > std::numeric_limits<int>::max() || ah > std::numeric_limits<int>::max()) {
                return false;
            }
            out_atlases.push_back({static_cast<int>(aw), static_cast<int>(ah)});
            for (int i = 0; i < count; ++i) {
                int col = i % cols;
                int row = i / cols;
                long long x_ll = static_cast<long long>(col) * cell_w;
                long long y_ll = static_cast<long long>(row) * cell_h;
                if (x_ll > std::numeric_limits<int>::max() || y_ll > std::numeric_limits<int>::max()) {
                    return false;
                }
                sprites[base + i].x = static_cast<int>(x_ll);
                sprites[base + i].y = static_cast<int>(y_ll);
                sprites[base + i].atlas_index = atlas_idx;
            }
        }
        return true;
    }

    std::vector<Sprite> remaining = sprites;
    std::vector<Sprite> all_packed;
    int atlas_index = 0;

    while (!remaining.empty()) {
        int min_width = std::numeric_limits<int>::max();
        size_t remaining_area = 0;
        for (const auto& s : remaining) {
            int rw = 0;
            int rh = 0;
            if (!checked_add_int(s.w, padding, rw) || !checked_add_int(s.h, padding, rh)) {
                return false;
            }
            min_width = std::min(min_width, rw);
            size_t sprite_area = 0;
            if (!checked_mul_size_t(static_cast<size_t>(s.w), static_cast<size_t>(s.h), sprite_area) ||
                sprite_area > std::numeric_limits<size_t>::max() - remaining_area) {
                return false;
            }
            remaining_area += sprite_area;
        }
        if (min_width <= 0 || min_width > max_w) {
            return false;
        }

        std::unordered_set<int> seen_widths;
        std::vector<int> width_candidates;
        auto add_width_candidate = [&](int width) {
            if (width < min_width || width > max_w) {
                return;
            }
            if (mode == Mode::POT) {
                width = next_power_of_two(width);
                if (width <= 0 || width < min_width || width > max_w) {
                    return;
                }
            }
            if (seen_widths.insert(width).second) {
                width_candidates.push_back(width);
            }
        };
        add_width_candidate(min_width);
        add_width_candidate(max_w);
        if (remaining_area > 0) {
            const long double root = std::sqrt(static_cast<long double>(remaining_area));
            if (root <= static_cast<long double>(std::numeric_limits<int>::max())) {
                add_width_candidate(static_cast<int>(std::ceil(root)));
            }
        }
        const int range = std::max(0, max_w - min_width);
        const int step = std::max(k_search_step_min, range / k_search_step_divisor);
        for (int mul : k_guided_search_offsets) {
            const long long w_ll =
                static_cast<long long>(min_width) +
                (static_cast<long long>(mul) * static_cast<long long>(step));
            if (w_ll < static_cast<long long>(std::numeric_limits<int>::min()) ||
                w_ll > static_cast<long long>(std::numeric_limits<int>::max())) {
                continue;
            }
            add_width_candidate(static_cast<int>(w_ll));
        }
        std::ranges::sort(width_candidates);

        struct MultipackCandidate {
            bool valid = false;
            int used_w = 0;
            int used_h = 0;
            size_t area = 0;
            size_t packed_area = 0;
            int packed_count = 0;
            std::vector<Sprite> packed;
            std::vector<Sprite> left;
        };
        auto better_multipack_candidate = [&](const MultipackCandidate& candidate,
                                              const MultipackCandidate& best) {
            if (!best.valid) {
                return true;
            }
            if (optimize_target == OptimizeTarget::GPU) {
                if (candidate.packed_count != best.packed_count) {
                    return candidate.packed_count > best.packed_count;
                }
                if (candidate.packed_area != best.packed_area) {
                    return candidate.packed_area > best.packed_area;
                }
                return pick_better_layout_candidate(
                    candidate.area, candidate.used_w, candidate.used_h, true,
                    best.area, best.used_w, best.used_h,
                    OptimizeTarget::GPU);
            }

            // For SPACE in multipack, avoid over-fragmenting into tiny dense atlases:
            // prioritize packing more content this round, then prefer lower area.
            if (candidate.packed_area != best.packed_area) {
                return candidate.packed_area > best.packed_area;
            }
            if (candidate.packed_count != best.packed_count) {
                return candidate.packed_count > best.packed_count;
            }
            if (candidate.area != best.area) {
                return candidate.area < best.area;
            }
            return pick_better_layout_candidate(
                candidate.area, candidate.used_w, candidate.used_h, true,
                best.area, best.used_w, best.used_h,
                OptimizeTarget::SPACE);
        };

        MultipackCandidate best_candidate;
        for (SortMode sort_mode : k_search_sort_modes) {
            if (enforce_sort_order && sort_mode != SortMode::None) {
                continue;
            }
            std::vector<Sprite> sorted = remaining;
            if (!sort_sprites_by_mode(sorted, sort_mode)) {
                continue;
            }
            for (int width : width_candidates) {
                for (RectHeuristic heuristic : k_rect_heuristics) {
                    PartialPackResult trial;
                    if (!pack_compact_maxrects_partial(
                            sorted, width, max_h, padding, heuristic, allow_rotate, trial)) {
                        continue;
                    }
                    int final_w = trial.used_w;
                    int final_h = trial.used_h;
                    if (mode == Mode::POT) {
                        final_w = next_power_of_two(final_w);
                        final_h = next_power_of_two(final_h);
                        if (final_w <= 0 || final_h <= 0 || final_w > max_w || final_h > max_h) {
                            continue;
                        }
                    }
                    size_t final_area = 0;
                    if (!checked_mul_size_t(static_cast<size_t>(final_w), static_cast<size_t>(final_h), final_area)) {
                        continue;
                    }
                    MultipackCandidate candidate;
                    candidate.valid = true;
                    candidate.used_w = final_w;
                    candidate.used_h = final_h;
                    candidate.area = final_area;
                    candidate.packed_area = trial.packed_area;
                    candidate.packed_count = static_cast<int>(trial.packed.size());
                    candidate.packed = std::move(trial.packed);
                    candidate.left = std::move(trial.remaining);
                    if (better_multipack_candidate(candidate, best_candidate)) {
                        best_candidate = std::move(candidate);
                    }
                }
            }
        }

        if (!best_candidate.valid || best_candidate.packed.empty()) {
            return false;
        }
        for (auto& s : best_candidate.packed) {
            s.atlas_index = atlas_index;
        }
        out_atlases.push_back({best_candidate.used_w, best_candidate.used_h});
        all_packed.insert(all_packed.end(), best_candidate.packed.begin(), best_candidate.packed.end());
        remaining = std::move(best_candidate.left);
        atlas_index++;
    }

    sprites = all_packed;
    return true;
}


bool search_pot_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                       int& out_width, int& out_height) {
    const int min_pot_width = next_power_of_two(request.max_sprite_width);
    const int min_pot_height = next_power_of_two(request.max_sprite_height);
    if (min_pot_width <= 0 || min_pot_height <= 0) {
        std::cerr << tr("Error: dimensions are too large\n");
        return false;
    }

    const SortedSprites pot_sorted = sort_for_search(sprites, request.enforce_sort_order);

    // First, find an upper bound that can pack, then search all POT
    // rectangles up to that area and pick the least wasteful successful fit.
    int side = std::max(min_pot_width, min_pot_height);
    int best_gpu_w = 0;
    int best_gpu_h = 0;
    size_t best_gpu_area = 0;
    bool have_best_gpu = false;
    std::vector<Sprite> best_gpu_sprites;

    int best_space_w = 0;
    int best_space_h = 0;
    size_t best_space_area = 0;
    bool have_best_space = false;
    std::vector<Sprite> best_space_sprites;

    size_t max_candidate_area = 0;
    std::vector<Sprite> trial_sprites;

    while (true) {
        if (request.max_width_limit > 0 && side > request.max_width_limit) {
            std::cerr << tr("Error: no POT layout fits within max width\n");
            return false;
        }
        if (request.max_height_limit > 0 && side > request.max_height_limit) {
            std::cerr << tr("Error: no POT layout fits within max height\n");
            return false;
        }
        for (size_t si = 0; si < k_search_sort_modes.size(); ++si) {
            if (skip_sort_mode(request.enforce_sort_order, k_search_sort_modes[si])) {
                continue;
            }
            trial_sprites.assign(pot_sorted[si].begin(), pot_sorted[si].end());
            if (!pack_binary_tree(trial_sprites, side, side, request.padding, request.allow_rotate)) {
                continue;
            }
            size_t area = static_cast<size_t>(side) * static_cast<size_t>(side);
            best_gpu_sprites = trial_sprites;
            best_space_sprites = std::move(trial_sprites);
            best_gpu_w = best_space_w = side;
            best_gpu_h = best_space_h = side;
            best_gpu_area = best_space_area = area;
            max_candidate_area = area;
            have_best_gpu = have_best_space = true;
            break;
        }
        if (have_best_gpu) {
            break;
        }
        if (side > std::numeric_limits<int>::max() / 2) {
            std::cerr << tr("Error: atlas dimensions overflow\n");
            return false;
        }
        side *= 2;
    }

    std::vector<int> pot_widths;
    std::vector<int> pot_heights;
    for (int w = min_pot_width; w > 0 && std::cmp_less_equal(w, max_candidate_area); w *= 2) {
        pot_widths.push_back(w);
        if (w > std::numeric_limits<int>::max() / 2) {
            break;
        }
    }
    for (int h = min_pot_height; h > 0 && std::cmp_less_equal(h, max_candidate_area); h *= 2) {
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
            if (request.max_width_limit > 0 && w > request.max_width_limit) {
                continue;
            }
            if (request.max_height_limit > 0 && h > request.max_height_limit) {
                continue;
            }
            const bool could_beat_gpu = pick_better_layout_candidate(area, w, h, have_best_gpu, best_gpu_area, best_gpu_w, best_gpu_h, OptimizeTarget::GPU);
            const bool could_beat_space = pick_better_layout_candidate(area, w, h, have_best_space, best_space_area, best_space_w, best_space_h, OptimizeTarget::SPACE);
            if (!could_beat_gpu && !could_beat_space) {
                continue;
            }

            for (size_t si = 0; si < k_search_sort_modes.size(); ++si) {
                if (skip_sort_mode(request.enforce_sort_order, k_search_sort_modes[si])) {
                    continue;
                }
                trial_sprites.assign(pot_sorted[si].begin(), pot_sorted[si].end());
                if (!pack_binary_tree(trial_sprites, w, h, request.padding, request.allow_rotate)) {
                    continue;
                }

                if (could_beat_gpu) {
                    best_gpu_sprites = trial_sprites;
                    best_gpu_w = w;
                    best_gpu_h = h;
                    best_gpu_area = area;
                    have_best_gpu = true;
                }
                if (could_beat_space) {
                    if (could_beat_gpu) {
                        best_space_sprites = best_gpu_sprites;
                    } else {
                        best_space_sprites = std::move(trial_sprites);
                    }
                    best_space_w = w;
                    best_space_h = h;
                    best_space_area = area;
                    have_best_space = true;
                }
                break;
            }
        }
    }

    if (!have_best_gpu && !have_best_space) {
        std::cerr << tr("Error: failed to compute pot layout\n");
        return false;
    }

    if (request.optimize_target == OptimizeTarget::GPU) {
        if (have_best_gpu) {
            sprites = std::move(best_gpu_sprites);
            out_width = best_gpu_w;
            out_height = best_gpu_h;
        } else {
            std::cerr << tr("Warning: no GPU-optimal POT layout found, using space-optimal fallback\n");
            sprites = std::move(best_space_sprites);
            out_width = best_space_w;
            out_height = best_space_h;
        }
    } else {
        if (have_best_space) {
            sprites = std::move(best_space_sprites);
            out_width = best_space_w;
            out_height = best_space_h;
        } else {
            std::cerr << tr("Warning: no space-optimal POT layout found, using GPU-optimal fallback\n");
            sprites = std::move(best_gpu_sprites);
            out_width = best_gpu_w;
            out_height = best_gpu_h;
        }
    }
    return true;
}

bool search_compact_layout(const SearchRequest& request, const std::vector<Sprite>& sprites,
                           CompactSearchResult& out) {
    if (request.sum_width <= 0 || request.sum_height <= 0) {
        std::cerr << tr("Error: compact bounds are invalid\n");
        return false;
    }
    const int max_width = request.max_sprite_width;
    const int width_upper_bound = request.width_upper_bound;
    const int height_upper_bound = request.height_upper_bound;
    const size_t total_area = request.total_area;
    const int padding = request.padding;
    const bool allow_rotate = request.allow_rotate;
    const bool enforce_sort_order = request.enforce_sort_order;
    unsigned int worker_count = resolve_worker_count(request.thread_limit);

    const SortedSprites sorted_sprites_by_mode = sort_for_search(sprites, enforce_sort_order);

    int seed_width = max_width;
    if (total_area > 0) {
        int root_width = 0;
        if (!ceil_sqrt(total_area, root_width)) {
            std::cerr << tr("Error: compact width is too large\n");
            return false;
        }
        seed_width = std::max(root_width, seed_width);
    }
    seed_width = std::min(seed_width, width_upper_bound);
    seed_width = std::max(seed_width, max_width);
    if (request.seed_hint != nullptr) {
        const int hint_width = seed_hint_width(*request.seed_hint, padding);
        if (hint_width >= max_width && hint_width <= width_upper_bound) {
            seed_width = hint_width;
        }
    }

    // Seed pass: every sort order and heuristic at the seed width.
    BestCandidates best;
    std::vector<Sprite> seed_sprites;
    for (size_t sort_idx = 0; sort_idx < k_search_sort_modes.size(); ++sort_idx) {
        if (skip_sort_mode(enforce_sort_order, k_search_sort_modes[sort_idx])) {
            continue;
        }
        for (RectHeuristic rect_heuristic : k_rect_heuristics) {
            seed_sprites.assign(sorted_sprites_by_mode[sort_idx].begin(), sorted_sprites_by_mode[sort_idx].end());
            int seed_used_w = 0;
            int seed_used_h = 0;
            if (!pack_compact_maxrects(seed_sprites, seed_width, padding, height_upper_bound, rect_heuristic, allow_rotate, seed_used_w, seed_used_h)) {
                continue;
            }
            best.consider(make_candidate(seed_used_w, seed_used_h, std::move(seed_sprites)));
        }
    }

    // Guided compact search (no brute-force width scan):
    // start from fast/seed anchors, then probe a small nearby window.
    // Keep searching even when the seed width fails, because other widths
    // can still produce a valid compact layout under tight atlas limits.
    int fast_target_width = max_width;
    if (total_area > 0) {
        int candidate = 0;
        if (ceil_sqrt(total_area, candidate)) {
            fast_target_width = std::max(candidate, fast_target_width);
        }
    }
    fast_target_width = std::min(fast_target_width, width_upper_bound);
    fast_target_width = std::max(fast_target_width, max_width);

    std::unordered_set<int> seen_widths;
    std::vector<int> width_candidates;
    auto add_width_candidate = [&](int width) {
        if (width < max_width || width > width_upper_bound) {
            return;
        }
        if (seen_widths.insert(width).second) {
            width_candidates.push_back(width);
        }
    };

    add_width_candidate(seed_width);
    add_width_candidate(fast_target_width);
    if (request.seed_hint != nullptr) {
        add_width_candidate(seed_hint_width(*request.seed_hint, padding));
    }

    const int range = std::max(0, width_upper_bound - fast_target_width);
    const int step = std::max(k_search_step_min, range / k_search_step_divisor);
    const std::array<int, 4> anchor_widths = {seed_width, fast_target_width, max_width, width_upper_bound};
    for (int anchor : anchor_widths) {
        for (int mul : k_guided_search_offsets) {
            const long long width_ll =
                static_cast<long long>(anchor) +
                (static_cast<long long>(mul) * static_cast<long long>(step));
            if (width_ll < static_cast<long long>(std::numeric_limits<int>::min()) ||
                width_ll > static_cast<long long>(std::numeric_limits<int>::max())) {
                continue;
            }
            add_width_candidate(static_cast<int>(width_ll));
        }
    }
    std::ranges::sort(width_candidates);

    if (!width_candidates.empty()) {
        worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(width_candidates.size()));
        std::vector<BestCandidates> worker_best(worker_count);
        const int min_square_side =
            total_area > 0
                ? static_cast<int>(std::ceil(std::sqrt(static_cast<long double>(total_area))))
                : 0;
        // Optimistic bound: the sprites' total area packed perfectly at this width.
        auto width_could_beat_best = [&](int width, const LayoutCandidate& local_best, const LayoutCandidate& shared_best, OptimizeTarget target) {
            const LayoutCandidate* best_so_far = select_better_candidate(local_best, shared_best, target);
            if (best_so_far == nullptr || total_area == 0) {
                return true;
            }
            const size_t width_size = static_cast<size_t>(width);
            const size_t min_height_size = (total_area + width_size - 1) / width_size;
            if (min_height_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            const int min_height = static_cast<int>(min_height_size);
            const int min_max_side = std::max(min_square_side, min_height);
            const int optimistic_w = std::min(width, min_max_side);
            const int optimistic_h = min_max_side;
            return pick_better_layout_candidate(
                total_area, optimistic_w, optimistic_h, true,
                best_so_far->area, best_so_far->w, best_so_far->h,
                target);
        };
        // Workers only read `best` (the seed pass result) while they run.
        auto width_could_beat = [&](int width, const BestCandidates& local) {
            return width_could_beat_best(width, local.gpu, best.gpu, OptimizeTarget::GPU) ||
                   width_could_beat_best(width, local.space, best.space, OptimizeTarget::SPACE);
        };
        auto run_guided_worker = [&](std::atomic<size_t>* next_width_index, size_t begin, size_t end, BestCandidates& local) {
            std::vector<Sprite> trial_sprites;
            std::vector<Sprite> shelf_sprites;
            while (true) {
                size_t width_index = begin;
                if (next_width_index != nullptr) {
                    width_index = next_width_index->fetch_add(1, std::memory_order_relaxed);
                    if (width_index >= end) {
                        break;
                    }
                } else if (width_index < end) {
                    ++begin;
                } else {
                    break;
                }

                const int width = width_candidates[width_index];
                if (!width_could_beat(width, local)) {
                    continue;
                }

                bool continue_width_search = true;
                for (size_t sort_idx : k_guided_sort_indices) {
                    if (enforce_sort_order && sort_idx != k_sort_mode_index_none) {
                        continue;
                    }

                    for (RectHeuristic rect_heuristic : k_rect_heuristics) {
                        trial_sprites.assign(sorted_sprites_by_mode[sort_idx].begin(), sorted_sprites_by_mode[sort_idx].end());
                        int used_w = 0;
                        int used_h = 0;
                        if (!pack_compact_maxrects(trial_sprites, width, padding, height_upper_bound, rect_heuristic, allow_rotate, used_w, used_h)) {
                            continue;
                        }
                        local.consider(make_candidate(used_w, used_h, std::move(trial_sprites)));
                        if (!width_could_beat(width, local)) {
                            continue_width_search = false;
                            break;
                        }
                    }
                    if (!continue_width_search) {
                        break;
                    }

                    shelf_sprites.assign(sorted_sprites_by_mode[sort_idx].begin(), sorted_sprites_by_mode[sort_idx].end());
                    int shelf_w = 0;
                    int shelf_h = 0;
                    if (!pack_fast_shelf(shelf_sprites, width, padding, allow_rotate, shelf_w, shelf_h)) {
                        continue;
                    }
                    if (shelf_h > height_upper_bound) {
                        continue;
                    }
                    local.consider(make_candidate(shelf_w, shelf_h, std::move(shelf_sprites)));
                    if (!width_could_beat(width, local)) {
                        break;
                    }
                }
            }
        };

        if (worker_count == 1) {
            run_guided_worker(nullptr, 0, width_candidates.size(), worker_best[0]);
        } else {
            std::atomic<size_t> next_width_index{0};
            sprat::core::run_workers(worker_count, [&](unsigned int worker_index) {
                run_guided_worker(&next_width_index, 0, width_candidates.size(), worker_best[worker_index]);
            });
        }
        for (BestCandidates& worker : worker_best) {
            if (worker.gpu.valid) {
                best.consider(std::move(worker.gpu));
            }
            if (worker.space.valid) {
                best.consider(std::move(worker.space));
            }
        }
    }

    out.best_gpu = std::move(best.gpu);
    out.best_space = std::move(best.space);
    return true;
}

const LayoutCandidate* select_compact_candidate(const CompactSearchResult& result,
                                                OptimizeTarget optimize_target) {
    if (optimize_target == OptimizeTarget::GPU) {
        if (result.best_gpu.valid) {
            return &result.best_gpu;
        }
        std::cerr << tr("Warning: no GPU-optimal layout found, using space-optimal fallback\n");
        return &result.best_space;
    }
    if (result.best_space.valid) {
        return &result.best_space;
    }
    std::cerr << tr("Warning: no space-optimal layout found, using GPU-optimal fallback\n");
    return &result.best_gpu;
}

bool search_fast_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                        int& out_width, int& out_height) {
    int target_width = request.max_sprite_width;
    if (request.total_area > 0) {
        int candidate = 0;
        if (!ceil_sqrt(request.total_area, candidate)) {
            std::cerr << tr("Error: fast width is too large\n");
            return false;
        }
        target_width = std::max(candidate, target_width);
    }
    target_width = std::min(target_width, request.width_upper_bound);
    if (request.seed_hint != nullptr) {
        const int hint_width = seed_hint_width(*request.seed_hint, request.padding);
        if (hint_width > target_width && hint_width <= request.width_upper_bound) {
            target_width = hint_width;
        }
    }

    std::vector<Sprite> sorted_sprites = sprites;
    if (request.enforce_stable_order) {
        sort_sprites_stable(sorted_sprites, request.stable_metric);
    } else if (!request.enforce_sort_order) {
        sort_sprites_by_mode(sorted_sprites, SortMode::Height);
    }

    // The narrowest width from the target up whose shelves fit the height bound.
    for (int width = target_width; width <= request.width_upper_bound; ++width) {
        std::vector<Sprite> trial_sprites = sorted_sprites;
        int packed_width = 0;
        int packed_height = 0;
        if (!pack_fast_shelf(trial_sprites, width, request.padding, request.allow_rotate, packed_width, packed_height)) {
            continue;
        }
        if (packed_height > request.height_upper_bound) {
            continue;
        }
        sprites = std::move(trial_sprites);
        out_width = packed_width;
        out_height = packed_height;
        return true;
    }
    std::cerr << tr("Error: failed to compute fast layout\n");
    return false;
}

bool search_grid_layout(const SearchRequest& request, std::vector<Sprite>& sprites,
                        int& out_width, int& out_height) {
    std::vector<Sprite> sorted_sprites = sprites;
    if (request.enforce_stable_order) {
        sort_sprites_stable(sorted_sprites, request.stable_metric);
    }
    if (!pack_grid(sorted_sprites, request.padding, request.width_upper_bound, request.height_upper_bound,
                   out_width, out_height)) {
        std::cerr << tr("Error: failed to compute grid layout\n");
        return false;
    }
    sprites = std::move(sorted_sprites);
    return true;
}
} // namespace sprat::layout
