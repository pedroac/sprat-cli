// Measuring source images (trim, nine-patch markers, hashes) and deduplicating sprites.
#pragma once

#include "commands/spratlayout/cache.h"
#include "commands/spratlayout/common.h"
#include "core/layout_parser.h"

#include <string>
#include <vector>

namespace sprat::layout {

struct SpriteLoadOptions {
    bool trim_transparent = false;
    // "none", "exact" or "perceptual"; the last two also compute the hashes deduplication needs.
    std::string deduplicate_mode = "none";
    unsigned int thread_limit = 0;  // 0 = hardware concurrency
};

// Measures every source, reusing up-to-date image cache entries and adding new ones.
// Sources that fail to load are reported as warnings and skipped.
std::vector<Sprite> load_sprites(const std::vector<ImageSource>& sources,
                                 const SpriteLoadOptions& options,
                                 ImageCache& cache_entries,
                                 long long now_unix);

// Replaces duplicate sprites with aliases of the first equal sprite. The image cache
// must hold the hashes computed by load_sprites with the same mode.
void deduplicate_sprites(std::vector<Sprite>& sprites,
                         const std::string& deduplicate_mode,
                         bool trim_transparent,
                         int dedup_threshold,
                         int dedup_tolerance,
                         const ImageCache& cache_entries,
                         std::vector<sprat::core::Alias>& out_aliases);

// Rounds w * scale to the nearest integer, at least 1. Returns false on overflow.
bool scale_dimension(int input, double scale, int& output);

} // namespace sprat::layout
