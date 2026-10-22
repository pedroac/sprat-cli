// Types shared by the spratlayout modules.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace sprat::layout {

namespace fs = std::filesystem;

enum class Mode : std::uint8_t { POT, COMPACT, FAST, GRID };
enum class OptimizeTarget : std::uint8_t { GPU, SPACE };
enum class ResolutionReference : std::uint8_t { Largest, Smallest };
enum class FrameSort : std::uint8_t { Name, None, Stable };
enum class StableMetric : std::uint8_t { Area, MaxSide, Height, Width, Perimeter };

constexpr int k_max_image_dimension = 100000;

struct Sprite {
    std::string path;
    int w{}, h{};
    int x = 0, y = 0;
    int trim_left = 0, trim_top = 0;
    int trim_right = 0, trim_bottom = 0;
    bool rotated = false;
    int atlas_index = 0;
    bool has_slice = false;
    int slice_left = 0, slice_top = 0, slice_right = 0, slice_bottom = 0;
};

struct Atlas {
    int width = 0;
    int height = 0;
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

// Positions of a previous layout, reused when the same sprites are packed again.
struct LayoutSeedEntry {
    std::string path;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int trim_left = 0;
    int trim_top = 0;
    int trim_right = 0;
    int trim_bottom = 0;
    bool rotated = false;
};

struct LayoutSeedCache {
    std::string signature;
    int padding = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    std::vector<LayoutSeedEntry> entries;
};

// Number of worker threads for a --threads value (0 = hardware concurrency).
inline unsigned int resolve_worker_count(unsigned int thread_limit) {
    unsigned int count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
    if (count == 0) {
        count = 1;
    }
#ifdef __EMSCRIPTEN__
    // Browser WASM builds typically lack pthreads; force single-threaded work.
    count = 1;
#endif
    return count;
}

inline std::string to_lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace sprat::layout
