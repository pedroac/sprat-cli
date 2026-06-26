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
    std::vector<std::pair<std::string, std::string>> aliases;  // (alias_path, canonical_path) pairs
};

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error);
bool parse_atlas_line(const std::string& line, int& width, int& height);
bool parse_scale_line(const std::string& line, double& scale);
bool parse_extrude_line(const std::string& line, int& extrude);
bool parse_multipack_line(const std::string& line, bool& multipack);
bool parse_alias_line(const std::string& line, std::string& alias_path, std::string& canonical_path, std::string& error);
bool parse_layout(std::istream& in, Layout& out, std::string& error);

} // namespace sprat::core
