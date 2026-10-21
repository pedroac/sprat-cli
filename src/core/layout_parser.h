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
    std::vector<Sprite> sprites;
    std::vector<Marker> global_markers;
    std::vector<Alias> aliases;
};

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error);
bool parse_atlas_line(const std::string& line, int& width, int& height);
bool parse_scale_line(const std::string& line, double& scale);
bool parse_extrude_line(const std::string& line, int& extrude);
bool parse_multipack_line(const std::string& line, bool& multipack);
bool parse_alias_line(const std::string& line, Alias& out, std::string& error);
bool parse_layout(std::istream& in, Layout& out, std::string& error);
// Appends one sprite per alias, sharing its canonical sprite's atlas region, and
// clears layout.aliases. Fails if an alias names a canonical sprite that is missing.
bool expand_layout_aliases(Layout& layout, std::string& error);

} // namespace sprat::core
