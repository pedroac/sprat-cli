#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace sprat::core {

struct Marker {
    std::string name;
    std::string type;
    std::string sprite_path;
    int x = 0;
    int y = 0;
    int radius = 0;
    int w = 0;
    int h = 0;
    std::vector<std::pair<int, int>> vertices;
};

struct Sprite {
    std::string path;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int src_x = 0;
    int src_y = 0;
    int trim_right = 0;
    int trim_bottom = 0;
    bool has_trim = false;
    bool rotated = false;
    int atlas_index = 0;
    bool dither = false;
    int colors = 0;
    int slice_left = 0;
    int slice_top = 0;
    int slice_right = 0;
    int slice_bottom = 0;
    bool has_slice = false;
    std::string slice_h = "stretch";
    std::string slice_v = "stretch";
    std::vector<Marker> markers;
    std::string alias_of;  // Non-empty if this is an alias of another sprite
};

// A duplicate sprite that reuses a canonical sprite's atlas region. The alias keeps
// its own trim offsets: identical trimmed pixels can sit at different places in
// their source images (e.g. a bobbing animation frame), so copying the canonical
// trim would shift the alias when an engine restores the original canvas.
struct Alias {
    std::string path;
    std::string canonical_path;
    bool has_trim = false;  // false: inherit the canonical sprite's trim
    int trim_left = 0;
    int trim_top = 0;
    int trim_right = 0;
    int trim_bottom = 0;
};

struct Atlas {
    int width = 0;
    int height = 0;
};

struct Layout {
    std::vector<Atlas> atlases;
    std::string root;
    bool has_root = false;
    double scale = 1.0;
    bool has_scale = false;
    int extrude = 0;
    bool has_extrude = false;
    bool multipack = false;
    bool has_multipack = false;
    // Direction of the 90-degree rotation applied to sprites marked "rotated".
    // Clockwise is the default; libGDX needs counter-clockwise.
    bool rotate_ccw = false;
    bool has_rotation = false;
    std::vector<Sprite> sprites;
    std::vector<Marker> global_markers;
    std::vector<Alias> aliases;
};

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error);
bool parse_atlas_line(const std::string& line, int& width, int& height);
bool parse_scale_line(const std::string& line, double& scale);
bool parse_extrude_line(const std::string& line, int& extrude);
bool parse_multipack_line(const std::string& line, bool& multipack);
bool parse_rotation_line(const std::string& line, bool& rotate_ccw);
bool parse_alias_line(const std::string& line, Alias& out, std::string& error);
bool parse_layout(std::istream& in, Layout& out, std::string& error);
// Appends one sprite per alias, sharing its canonical sprite's atlas region, and
// clears layout.aliases. Fails if an alias names a canonical sprite that is missing.
bool expand_layout_aliases(Layout& layout, std::string& error);

// Android nine-patch images (".9.png") carry a 1px stretch-marker border around the
// content. Layouts keep that border inside the trim offsets, since spratpack crops from
// the whole file; slice insets are relative to the content.
inline bool is_nine_patch_path(std::string_view path) {
    constexpr std::string_view suffix = ".9.png";
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
}

// Largest coordinate or size spratconvert exports, in atlas pixels (2^24). It matches
// sprat-lib's SPRAT_COORD_LIMIT, so every sheet spratconvert writes also loads there:
// sums of a few such values fit in an int, and each one converts to float exactly.
constexpr int k_max_coordinate = 1 << 24;
constexpr bool within_coordinate_limit(long long value) {
    return value >= -k_max_coordinate && value <= k_max_coordinate;
}

// Fails when a sprite's exported geometry would exceed k_max_coordinate: its scaled
// source size (upright packed size plus scaled trims) must stay within the limit.
bool check_sprite_limits(const Sprite& s, double scale, std::string& error);

// Layout trims, slices and markers are in original-image pixels, while the packed size is
// already scaled. Converts one such value into atlas pixels, rounding to the nearest pixel
// and saturating instead of overflowing for absurd layout scales.
int scale_to_atlas(int value, double scale);

// Trim margins of the sprite as engines (and unpacked sprites) see it, in atlas pixels:
// a nine-patch's 1px marker border is removed (it is not part of the image an engine
// gets; spratlayout only strips it when all four sides have it), then the margins are
// scaled like the packed size.
struct MetadataTrim {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};
MetadataTrim metadata_trim(const Sprite& s, double scale);

// Nine-slice insets in atlas pixels. Each inset is rounded on its own, which can push a
// pair past the sprite by a pixel, so the right and bottom insets give up any excess:
// left + right <= source_w and top + bottom <= source_h always hold, letting loaders
// such as sprat-lib reject insets that don't fit.
struct MetadataSlice {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};
MetadataSlice metadata_slice(const Sprite& s, double scale, int source_w, int source_h);

// Fails when a sprite's slice insets don't fit its original image (for a nine-patch, the
// content inside the marker border): left + right must not exceed its width, nor
// top + bottom its height.
bool check_slice_fits(const Sprite& s, double scale, std::string& error);

} // namespace sprat::core
