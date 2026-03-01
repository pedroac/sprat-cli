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
    std::vector<Marker> markers;
};

struct Layout {
    int atlas_width = 0;
    int atlas_height = 0;
    double scale = 1.0;
    bool has_scale = false;
    std::vector<Sprite> sprites;
    std::vector<Marker> global_markers;
};

bool parse_int(const std::string& token, int& out);
bool parse_double(const std::string& token, double& out);
bool parse_pair(const std::string& token, int& a, int& b);
bool parse_quoted(std::string_view input, size_t& pos, std::string& out, std::string& error);
bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error);
bool parse_atlas_line(const std::string& line, int& width, int& height);
bool parse_scale_line(const std::string& line, double& scale);
bool parse_layout(std::istream& in, Layout& out, std::string& error);

} // namespace sprat::core
