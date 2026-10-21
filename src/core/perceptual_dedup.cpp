#include "perceptual_dedup.h"

#include <bit>
#include <cstdlib>

namespace sprat::core {

uint64_t compute_dhash(const unsigned char* rgba, int w, int h) {
    if (rgba == nullptr || w <= 0 || h <= 0) {
        return 0;
    }
    // Sample a 9x8 grid (nearest-neighbor) of alpha-premultiplied luma.
    double grid[8][9];
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 9; ++col) {
            const int px = (col * (w - 1)) / 8;
            const int py = (row * (h - 1)) / 7;
            const unsigned char* p =
                rgba + (static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(px)) * 4;
            const double a = p[3] / 255.0;
            grid[row][col] = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) * a;
        }
    }
    // One bit per adjacent column pair: 1 if left < right.
    uint64_t hash = 0;
    int bit = 0;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if (grid[row][col] < grid[row][col + 1]) {
                hash |= (uint64_t(1) << bit);
            }
            ++bit;
        }
    }
    return hash;
}

bool rgba_within_tolerance(const unsigned char* a,
                           const unsigned char* b,
                           size_t pixel_count,
                           int tolerance) {
    for (size_t i = 0; i < pixel_count; ++i) {
        const unsigned char* pa = a + i * 4;
        const unsigned char* pb = b + i * 4;
        if (pa[3] == 0 && pb[3] == 0) {
            continue;
        }
        for (int c = 0; c < 4; ++c) {
            if (std::abs(static_cast<int>(pa[c]) - static_cast<int>(pb[c])) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

std::vector<size_t> group_near_duplicates(
    const std::vector<DedupCandidate>& items,
    int hash_threshold,
    const std::function<bool(size_t canonical, size_t candidate)>& pixels_match) {
    std::vector<size_t> canonical_of(items.size());
    std::vector<size_t> canonicals;
    for (size_t i = 0; i < items.size(); ++i) {
        canonical_of[i] = i;
        const DedupCandidate& item = items[i];
        if (!item.has_hash) {
            continue;
        }
        bool matched = false;
        for (size_t c : canonicals) {
            const DedupCandidate& canon = items[c];
            if (canon.w != item.w || canon.h != item.h) {
                continue;
            }
            if (std::popcount(canon.hash ^ item.hash) > hash_threshold) {
                continue;
            }
            if (pixels_match(c, i)) {
                canonical_of[i] = c;
                matched = true;
                break;
            }
        }
        if (!matched) {
            canonicals.push_back(i);
        }
    }
    return canonical_of;
}

} // namespace sprat::core
