#pragma once

#include <cstddef>

namespace sprat::core {

// Largest images spratframes reads and spratunpack writes. Sizes beyond these come from
// corrupt or hostile input (a frames file whose sourceSize says 100000x100000) and would
// otherwise allocate tens of gigabytes; width * 4 bytes per pixel also stays within int,
// which stb's stride parameters use.
constexpr int k_max_image_dimension = 32768;
constexpr size_t k_max_total_pixels = 100000000;  // ~100 megapixels, 400 MB of RGBA

// True if a width x height RGBA image is within the limits above.
constexpr bool image_size_within_limits(int width, int height) {
    return width > 0 && height > 0 && width <= k_max_image_dimension &&
           height <= k_max_image_dimension &&
           static_cast<size_t>(width) * static_cast<size_t>(height) <= k_max_total_pixels;
}

} // namespace sprat::core
