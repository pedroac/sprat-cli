// spratpack.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 spratpack.cpp -o spratpack

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <cstring>
#include <cctype>
#include <limits>
#include <array>
#include <algorithm>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
};

bool checked_mul_size_t(size_t a, size_t b, size_t& out) {
    if (a == 0 || b <= std::numeric_limits<size_t>::max() / a) {
        out = a * b;
        return true;
    }
    return false;
}

bool checked_add_size_t(size_t a, size_t b, size_t& out) {
    if (b <= std::numeric_limits<size_t>::max() - a) {
        out = a + b;
        return true;
    }
    return false;
}

bool parse_int(const std::string& token, int& out) {
    if (token.empty()) {
        return false;
    }
    std::istringstream iss(token);
    int value = 0;
    char extra = '\0';
    if (!(iss >> value)) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    out = value;
    return true;
}

bool parse_double(const std::string& token, double& out) {
    if (token.empty()) {
        return false;
    }
    std::istringstream iss(token);
    double value = 0.0;
    char extra = '\0';
    if (!(iss >> value)) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool parse_pair(const std::string& token, int& a, int& b) {
    size_t comma = token.find(',');
    if (comma == std::string::npos || comma == 0 || comma + 1 >= token.size()) {
        return false;
    }
    if (token.find(',', comma + 1) != std::string::npos) {
        return false;
    }
    return parse_int(token.substr(0, comma), a) && parse_int(token.substr(comma + 1), b);
}

bool parse_quoted(std::string_view input, size_t& pos, std::string& out, std::string& error) {
    if (pos >= input.size() || input[pos] != '"') {
        error = "expected opening quote for sprite path";
        return false;
    }

    ++pos;
    out.clear();

    while (pos < input.size()) {
        char c = input[pos++];
        if (c == '\\') {
            if (pos >= input.size()) {
                error = "unterminated escape sequence in sprite path";
                return false;
            }
            char escaped = input[pos++];
            if (escaped == '"' || escaped == '\\') {
                out.push_back(escaped);
            } else {
                out.push_back('\\');
                out.push_back(escaped);
            }
        } else if (c == '"') {
            return true;
        } else {
            out.push_back(c);
        }
    }

    error = "unterminated quoted sprite path";
    return false;
}

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error) {
    constexpr std::string_view prefix = "sprite";
    if (line.rfind(prefix, 0) != 0) {
        error = "line does not start with sprite";
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    std::string path;
    if (pos < line.size() && line[pos] == '"') {
        if (!parse_quoted(line, pos, path, error)) {
            return false;
        }
    } else {
        error = "sprite path must be quoted";
        return false;
    }

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    Sprite parsed;
    parsed.path = path;
    std::vector<std::string> tokens;
    std::istringstream tail(line.substr(pos));
    std::string token;
    while (tail >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        error = "sprite line is missing numeric fields";
        return false;
    }

    if (tokens[0].find(',') != std::string::npos) {
        // New format: x,y w,h [left,top right,bottom]
        if (tokens.size() != 2 && tokens.size() != 4) {
            error = "sprite line must contain position/size and optional trim offsets";
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y) ||
            !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = "invalid position or size pair";
            return false;
        }

        if (tokens.size() == 4) {
            if (!parse_pair(tokens[2], parsed.src_x, parsed.src_y) ||
                !parse_pair(tokens[3], parsed.trim_right, parsed.trim_bottom)) {
                error = "invalid trim offset pair";
                return false;
            }
            parsed.has_trim = true;
        }
    } else {
        // Legacy format: x y w h [src_x src_y]
        if (tokens.size() != 4 && tokens.size() != 6) {
            error = "legacy sprite line has invalid field count";
            return false;
        }
        if (!parse_int(tokens[0], parsed.x) ||
            !parse_int(tokens[1], parsed.y) ||
            !parse_int(tokens[2], parsed.w) ||
            !parse_int(tokens[3], parsed.h)) {
            error = "legacy sprite line has invalid numeric fields";
            return false;
        }
        if (tokens.size() == 6) {
            if (!parse_int(tokens[4], parsed.src_x) ||
                !parse_int(tokens[5], parsed.src_y)) {
                error = "legacy sprite line has invalid crop offsets";
                return false;
            }
            parsed.has_trim = true;
        }
    }

    out = parsed;
    return true;
}

bool parse_atlas_line(const std::string& line, int& width, int& height) {
    std::istringstream iss(line);
    std::string tag;
    std::string size_token;
    std::string extra;

    if (!(iss >> tag >> size_token)) {
        return false;
    }
    if (tag != "atlas") {
        return false;
    }
    if (!parse_pair(size_token, width, height)) {
        // Backward compatibility: atlas <w> <h>
        if (!parse_int(size_token, width) || !(iss >> height)) {
            return false;
        }
    }
    if (iss >> extra) {
        return false;
    }

    return true;
}

bool parse_scale_line(const std::string& line, double& scale) {
    std::istringstream iss(line);
    std::string tag;
    std::string value_token;
    std::string extra;

    if (!(iss >> tag >> value_token)) {
        return false;
    }
    if (tag != "scale") {
        return false;
    }
    if (!parse_double(value_token, scale) || scale <= 0.0) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return true;
}

bool parse_line_color(const std::string& value, std::array<unsigned char, 4>& out) {
    std::array<int, 4> parts = {0, 0, 0, 255};
    int part_count = 0;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        size_t end = (comma == std::string::npos) ? value.size() : comma;
        if (end == start || part_count >= 4) {
            return false;
        }

        std::string token = value.substr(start, end - start);
        int channel = 0;
        if (!parse_int(token, channel) || channel < 0 || channel > 255) {
            return false;
        }
        parts[part_count++] = channel;

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    if (part_count != 3 && part_count != 4) {
        return false;
    }

    out[0] = static_cast<unsigned char>(parts[0]);
    out[1] = static_cast<unsigned char>(parts[1]);
    out[2] = static_cast<unsigned char>(parts[2]);
    out[3] = static_cast<unsigned char>(parts[3]);
    return true;
}

void draw_sprite_outline(
    std::vector<unsigned char>& atlas,
    int atlas_width,
    int atlas_height,
    const Sprite& s,
    int line_width,
    const std::array<unsigned char, 4>& color
) {
    if (line_width <= 0) {
        return;
    }

    auto set_pixel = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= atlas_width || py >= atlas_height) {
            return;
        }
        size_t pixel_index = static_cast<size_t>(py) * static_cast<size_t>(atlas_width) + static_cast<size_t>(px);
        size_t offset = pixel_index * static_cast<size_t>(4);
        atlas[offset + 0] = color[0];
        atlas[offset + 1] = color[1];
        atlas[offset + 2] = color[2];
        atlas[offset + 3] = color[3];
    };

    int max_t = std::min(line_width, std::min(s.w, s.h));
    for (int t = 0; t < max_t; ++t) {
        int left = s.x + t;
        int right = s.x + s.w - 1 - t;
        int top = s.y + t;
        int bottom = s.y + s.h - 1 - t;

        for (int x = left; x <= right; ++x) {
            set_pixel(x, top);
            set_pixel(x, bottom);
        }
        for (int y = top; y <= bottom; ++y) {
            set_pixel(left, y);
            set_pixel(right, y);
        }
    }
}

int main(int argc, char** argv) {
    bool draw_frame_lines = false;
    int line_width = 1;
    std::array<unsigned char, 4> line_color = {255, 0, 0, 255};

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frame-lines") {
            draw_frame_lines = true;
        } else if (arg == "--line-width" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_int(value, line_width) || line_width <= 0) {
                std::cerr << "Invalid line width: " << value << "\n";
                return 1;
            }
        } else if (arg == "--line-color" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_line_color(value, line_color)) {
                std::cerr << "Invalid line color: " << value << "\n";
                std::cerr << "Expected format: R,G,B or R,G,B,A with 0-255 channels\n";
                return 1;
            }
        } else {
            std::cerr << "Usage: spratpack [--frame-lines] [--line-width N] [--line-color R,G,B[,A]]\n";
            return 1;
        }
    }

    std::string line;
    int atlas_width = 0;
    int atlas_height = 0;
    double layout_scale = 1.0;
    bool has_scale = false;
    std::vector<Sprite> sprites;

    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        if (line.rfind("atlas", 0) == 0) {
            if (!parse_atlas_line(line, atlas_width, atlas_height)) {
                std::cerr << "Invalid atlas line: " << line << "\n";
                return 1;
            }
        } else if (line.rfind("scale", 0) == 0) {
            if (has_scale) {
                std::cerr << "Duplicate scale line\n";
                return 1;
            }
            if (!parse_scale_line(line, layout_scale)) {
                std::cerr << "Invalid scale line: " << line << "\n";
                return 1;
            }
            has_scale = true;
        } else if (line.rfind("sprite", 0) == 0) {
            Sprite s;
            std::string error;
            if (!parse_sprite_line(line, s, error)) {
                std::cerr << "Invalid sprite line: " << error << "\n";
                return 1;
            }
            sprites.push_back(s);
        } else {
            std::cerr << "Unknown line: " << line << "\n";
            return 1;
        }
    }

    if (atlas_width <= 0 || atlas_height <= 0) {
        std::cerr << "Invalid atlas size\n";
        return 1;
    }

    size_t pixel_count = 0;
    size_t byte_count = 0;
    if (!checked_mul_size_t(static_cast<size_t>(atlas_width), static_cast<size_t>(atlas_height), pixel_count) ||
        !checked_mul_size_t(pixel_count, static_cast<size_t>(4), byte_count)) {
        std::cerr << "Atlas size is too large\n";
        return 1;
    }

    std::vector<unsigned char> atlas(byte_count, 0);

    for (const auto& s : sprites) {
        if (s.x < 0 || s.y < 0 || s.w <= 0 || s.h <= 0 || s.src_x < 0 || s.src_y < 0 ||
            s.trim_right < 0 || s.trim_bottom < 0) {
            std::cerr << "Invalid sprite bounds: " << s.path << "\n";
            return 1;
        }
        if (s.w > atlas_width || s.h > atlas_height ||
            s.x > atlas_width - s.w || s.y > atlas_height - s.h) {
            std::cerr << "Sprite out of atlas bounds: " << s.path << "\n";
            return 1;
        }

        int w, h, channels;
        unsigned char* data = stbi_load(s.path.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::cerr << "Failed to load: " << s.path << "\n";
            return 1;
        }
        int source_x = s.has_trim ? s.src_x : 0;
        int source_y = s.has_trim ? s.src_y : 0;
        int source_w = s.has_trim ? (w - s.src_x - s.trim_right) : w;
        int source_h = s.has_trim ? (h - s.src_y - s.trim_bottom) : h;

        if (source_x < 0 || source_y < 0 || source_w <= 0 || source_h <= 0) {
            std::cerr << "Crop out of bounds: " << s.path << "\n";
            stbi_image_free(data);
            return 1;
        }
        if (source_x > w - source_w || source_y > h - source_h) {
            std::cerr << "Trim offsets out of bounds: " << s.path << "\n";
            stbi_image_free(data);
            return 1;
        }

        size_t scaled_source_w = static_cast<size_t>(s.w);
        size_t scaled_source_h = static_cast<size_t>(s.h);
        if (layout_scale > 0.0 && layout_scale != 1.0) {
            // Validate that scaled destination dimensions are plausible for the
            // declared source crop rectangle to guard malformed inputs.
            if (source_w == 0 || source_h == 0) {
                std::cerr << "Invalid scaled source crop: " << s.path << "\n";
                stbi_image_free(data);
                return 1;
            }
        }

        if (scaled_source_w == 0 || scaled_source_h == 0) {
            std::cerr << "Invalid destination sprite size: " << s.path << "\n";
            stbi_image_free(data);
            return 1;
        }

        size_t source_pixels = 0;
        size_t source_bytes = 0;
        if (!checked_mul_size_t(static_cast<size_t>(w), static_cast<size_t>(h), source_pixels) ||
            !checked_mul_size_t(source_pixels, static_cast<size_t>(4), source_bytes)) {
            std::cerr << "Source image is too large: " << s.path << "\n";
            stbi_image_free(data);
            return 1;
        }

        for (int row = 0; row < s.h; ++row) {
            int sample_y = source_y + (row * source_h) / s.h;
            for (int col = 0; col < s.w; ++col) {
                int sample_x = source_x + (col * source_w) / s.w;

                size_t dest_pixels = 0;
                size_t dest_offset = 0;
                if (!checked_mul_size_t(static_cast<size_t>(s.y + row), static_cast<size_t>(atlas_width), dest_pixels)) {
                    std::cerr << "Atlas indexing overflow: " << s.path << "\n";
                    stbi_image_free(data);
                    return 1;
                }
                dest_pixels += static_cast<size_t>(s.x + col);
                if (!checked_mul_size_t(dest_pixels, static_cast<size_t>(4), dest_offset) ||
                    dest_offset > atlas.size() ||
                    static_cast<size_t>(4) > atlas.size() - dest_offset) {
                    std::cerr << "Atlas indexing out of bounds: " << s.path << "\n";
                    stbi_image_free(data);
                    return 1;
                }

                size_t src_pixels = 0;
                size_t src_offset = 0;
                if (!checked_mul_size_t(static_cast<size_t>(sample_y), static_cast<size_t>(w), src_pixels) ||
                    !checked_add_size_t(src_pixels, static_cast<size_t>(sample_x), src_pixels) ||
                    !checked_mul_size_t(src_pixels, static_cast<size_t>(4), src_offset) ||
                    src_offset > source_bytes ||
                    static_cast<size_t>(4) > source_bytes - src_offset) {
                    std::cerr << "Source indexing overflow: " << s.path << "\n";
                    stbi_image_free(data);
                    return 1;
                }

                atlas[dest_offset + 0] = data[src_offset + 0];
                atlas[dest_offset + 1] = data[src_offset + 1];
                atlas[dest_offset + 2] = data[src_offset + 2];
                atlas[dest_offset + 3] = data[src_offset + 3];
            }
        }

        stbi_image_free(data);
    }

    if (draw_frame_lines) {
        for (const auto& s : sprites) {
            draw_sprite_outline(atlas, atlas_width, atlas_height, s, line_width, line_color);
        }
    }

    auto write_callback = [](void* context, void* data, int size) {
        std::ostream* out = static_cast<std::ostream*>(context);
        out->write(static_cast<char*>(data), size);
    };

    if (!stbi_write_png_to_func(write_callback, &std::cout,
                                atlas_width, atlas_height, 4,
                                atlas.data(), atlas_width * 4)) {
        std::cerr << "Failed to write PNG\n";
        return 1;
    }

    return 0;
}
