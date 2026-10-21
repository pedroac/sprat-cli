#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sprat::core {

// Default maximum Hamming distance between two dHashes for a pair to be a candidate.
inline constexpr int k_default_dedup_hash_threshold = 5;
// Default maximum per-channel difference (0-255) for two pixels to count as equal.
inline constexpr int k_default_dedup_pixel_tolerance = 8;

// 64-bit difference hash (dHash) of an RGBA buffer. Cheap and approximate: it only
// sees a 9x8 grid of alpha-premultiplied luma, so it is a candidate filter, never
// proof that two images look the same.
uint64_t compute_dhash(const unsigned char* rgba, int w, int h);

// True when every pixel of a and b differs by at most `tolerance` in each RGBA
// channel. Pixels that are fully transparent in both buffers are equal regardless
// of their (invisible) RGB values.
bool rgba_within_tolerance(const unsigned char* a,
                           const unsigned char* b,
                           size_t pixel_count,
                           int tolerance);

struct DedupCandidate {
    int w = 0;
    int h = 0;
    uint64_t hash = 0;
    bool has_hash = false;
};

// Groups near-duplicate items. Returns, for each item, the index of its canonical
// item (its own index when it is canonical). Items are visited in order and each
// one is compared only against existing canonicals, so every member of a group is
// within tolerance of its canonical (no transitive chaining).
//
// A pair is considered only if sizes match and the hash distance is within
// `hash_threshold`; `pixels_match(canonical, candidate)` must then confirm it.
std::vector<size_t> group_near_duplicates(
    const std::vector<DedupCandidate>& items,
    int hash_threshold,
    const std::function<bool(size_t canonical, size_t candidate)>& pixels_match);

} // namespace sprat::core
