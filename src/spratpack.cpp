// spratpack.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 src/spratpack.cpp -o spratpack

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#ifndef _O_BINARY
#define _O_BINARY 0x8000
#endif
#ifndef _fileno
#define _fileno fileno
#endif
#ifndef _setmode
#define _setmode setmode
#endif
#endif
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <cstring>
#include <cctype>
#include <limits>
#include <array>
#include <cmath>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

constexpr size_t NUM_CHANNELS = 4;
constexpr size_t CHANNEL_R = 0;
constexpr size_t CHANNEL_G = 1;
constexpr size_t CHANNEL_B = 2;
constexpr size_t CHANNEL_A = 3;
constexpr int MAX_CHANNEL_VALUE = 255;

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
    if (!line.starts_with(prefix)) {
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
        constexpr size_t MODERN_SPRITE_TOKENS_MIN = 2;
        constexpr size_t MODERN_SPRITE_TOKENS_MAX = 4;
        if (tokens.size() != MODERN_SPRITE_TOKENS_MIN && tokens.size() != MODERN_SPRITE_TOKENS_MAX) {
            error = "sprite line must contain position/size and optional trim offsets";
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y)
            || !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = "invalid position or size pair";
            return false;
        }

        if (tokens.size() == MODERN_SPRITE_TOKENS_MAX) {
            if (!parse_pair(tokens[2], parsed.src_x, parsed.src_y)
                || !parse_pair(tokens[3], parsed.trim_right, parsed.trim_bottom)) {
                error = "invalid trim offset pair";
                return false;
            }
            parsed.has_trim = true;
        }
    } else {
        // Legacy format: x y w h [src_x src_y]
        constexpr size_t LEGACY_SPRITE_TOKENS_MIN = 4;
        constexpr size_t LEGACY_SPRITE_TOKENS_MAX = 6;
        if (tokens.size() != LEGACY_SPRITE_TOKENS_MIN && tokens.size() != LEGACY_SPRITE_TOKENS_MAX) {
            error = "legacy sprite line has invalid field count";
            return false;
        }
        if (!parse_int(tokens[0], parsed.x)
            || !parse_int(tokens[1], parsed.y)
            || !parse_int(tokens[2], parsed.w)
            || !parse_int(tokens[3], parsed.h)) {
            error = "legacy sprite line has invalid numeric fields";
            return false;
        }
        if (tokens.size() == LEGACY_SPRITE_TOKENS_MAX) {
            if (!parse_int(tokens[4], parsed.src_x)
                || !parse_int(tokens[5], parsed.src_y)) {
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
    constexpr int MAX_CHANNELS = 4;
    std::array<int, MAX_CHANNELS> parts = {0, 0, 0, MAX_CHANNEL_VALUE};
    int part_count = 0;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        size_t end = (comma == std::string::npos) ? value.size() : comma;
        if (end == start || part_count >= MAX_CHANNELS) {
            return false;
        }

        std::string token = value.substr(start, end - start);
        int channel = 0;
        if (!parse_int(token, channel) || channel < 0 || channel > MAX_CHANNEL_VALUE) {
            return false;
        }
        parts[part_count++] = channel;

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    constexpr int MIN_REQUIRED_CHANNELS = 3;
    if (part_count != MIN_REQUIRED_CHANNELS && part_count != MAX_CHANNELS) {
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
        size_t pixel_index = (static_cast<size_t>(py) * static_cast<size_t>(atlas_width)) + static_cast<size_t>(px);
        size_t offset = pixel_index * NUM_CHANNELS;
        atlas[offset + CHANNEL_R] = color[0];
        atlas[offset + CHANNEL_G] = color[1];
        atlas[offset + CHANNEL_B] = color[2];
        atlas[offset + CHANNEL_A] = color[3];
    };

    int max_t = std::min({line_width, s.w, s.h});
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

bool rectangles_overlap(const Sprite& a, const Sprite& b) {
    const int a_right = a.x + a.w;
    const int a_bottom = a.y + a.h;
    const int b_right = b.x + b.w;
    const int b_bottom = b.y + b.h;
    return a_right > b.x && b_right > a.x && a_bottom > b.y && b_bottom > a.y;
}

bool sprites_have_overlap(const std::vector<Sprite>& sprites) {
    if (sprites.size() < 2) {
        return false;
    }
    std::vector<size_t> order(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](size_t lhs, size_t rhs) {
        if (sprites[lhs].x != sprites[rhs].x) {
            return sprites[lhs].x < sprites[rhs].x;
        }
        return sprites[lhs].y < sprites[rhs].y;
    });

    for (size_t i = 0; i < order.size(); ++i) {
        const Sprite& a = sprites[order[i]];
        const int a_right = a.x + a.w;
        for (size_t j = i + 1; j < order.size(); ++j) {
            const Sprite& b = sprites[order[j]];
            if (b.x >= a_right) {
                break;
            }
            if (rectangles_overlap(a, b)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

void print_usage() {
    std::cout << "Usage: spratpack [OPTIONS]\n"
              << "\n"
              << "Read layout text from stdin and write a PNG atlas to stdout.\n"
              << "\n"
              << "Options:\n"
              << "  --frame-lines          Draw rectangle outlines for each sprite\n"
              << "  --line-width N         Outline thickness in pixels (default: 1)\n"
              << "  --line-color R,G,B[,A] Outline color channels (0-255, default: 255,0,0,255)\n"
              << "  --threads N            Number of worker threads\n"
              << "  --help, -h             Show this help message\n";
}

int main(int argc, char** argv) {
    bool draw_frame_lines = false;
    int line_width = 1;
    constexpr unsigned char DEFAULT_COLOR_RED = 255;
    constexpr unsigned char DEFAULT_COLOR_ALPHA = 255;
    std::array<unsigned char, 4> line_color = {DEFAULT_COLOR_RED, 0, 0, DEFAULT_COLOR_ALPHA};
    unsigned int thread_limit = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--frame-lines") {
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
                std::cerr << "Expected format: R,G,B or R,G,B,A with 0-" << MAX_CHANNEL_VALUE << " channels\n";
                return 1;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed <= 0) {
                std::cerr << "Invalid thread count: " << value << "\n";
                return 1;
            }
            thread_limit = static_cast<unsigned int>(parsed);
        } else {
            print_usage();
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
        if (line.starts_with("atlas")) {
            if (!parse_atlas_line(line, atlas_width, atlas_height)) {
                std::cerr << "Invalid atlas line: " << line << "\n";
                return 1;
            }
        } else if (line.starts_with("scale")) {
            if (has_scale) {
                std::cerr << "Duplicate scale line\n";
                return 1;
            }
            if (!parse_scale_line(line, layout_scale)) {
                std::cerr << "Invalid scale line: " << line << "\n";
                return 1;
            }
            has_scale = true;
        } else if (line.starts_with("sprite")) {
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
    if (!checked_mul_size_t(static_cast<size_t>(atlas_width), static_cast<size_t>(atlas_height), pixel_count)
        || !checked_mul_size_t(pixel_count, NUM_CHANNELS, byte_count)) {
        std::cerr << "Atlas size is too large\n";
        return 1;
    }

    std::vector<unsigned char> atlas(byte_count, 0);

    for (const auto& s : sprites) {
        if (s.x < 0 || s.y < 0 || s.w <= 0 || s.h <= 0 || s.src_x < 0 || s.src_y < 0
            || s.trim_right < 0 || s.trim_bottom < 0) {
            std::cerr << "Invalid sprite bounds: " << s.path << "\n";
            return 1;
        }
        if (s.w > atlas_width || s.h > atlas_height
            || s.x > atlas_width - s.w || s.y > atlas_height - s.h) {
            std::cerr << "Sprite out of atlas bounds: " << s.path << "\n";
            return 1;
        }
    }

    auto blit_sprite = [&](const Sprite& s, std::string& error_out) -> bool {
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* data = stbi_load(s.path.c_str(), &w, &h, &channels, static_cast<int>(NUM_CHANNELS));
        if (!data) {
            error_out = "Failed to load: " + s.path;
            return false;
        }
        int source_x = s.has_trim ? s.src_x : 0;
        int source_y = s.has_trim ? s.src_y : 0;
        int source_w = s.has_trim ? (w - s.src_x - s.trim_right) : w;
        int source_h = s.has_trim ? (h - s.src_y - s.trim_bottom) : h;

        if (source_x < 0 || source_y < 0 || source_w <= 0 || source_h <= 0) {
            error_out = "Crop out of bounds: " + s.path;
            stbi_image_free(data);
            return false;
        }
        if (source_x > w - source_w || source_y > h - source_h) {
            error_out = "Trim offsets out of bounds: " + s.path;
            stbi_image_free(data);
            return false;
        }

        auto scaled_source_w = static_cast<size_t>(s.w);
        auto scaled_source_h = static_cast<size_t>(s.h);
        if (layout_scale > 0.0 && layout_scale != 1.0) {
            // Validate that scaled destination dimensions are plausible for the
            // declared source crop rectangle to guard malformed inputs.
            if (source_w == 0 || source_h == 0) {
                error_out = "Invalid scaled source crop: " + s.path;
                stbi_image_free(data);
                return false;
            }
        }

        if (scaled_source_w == 0 || scaled_source_h == 0) {
            error_out = "Invalid destination sprite size: " + s.path;
            stbi_image_free(data);
            return false;
        }

        size_t source_pixels = 0;
        size_t source_bytes = 0;
        if (!checked_mul_size_t(static_cast<size_t>(w), static_cast<size_t>(h), source_pixels)
            || !checked_mul_size_t(source_pixels, NUM_CHANNELS, source_bytes)) {
            error_out = "Source image is too large: " + s.path;
            stbi_image_free(data);
            return false;
        }

        const bool copy_rows_direct = (source_w == s.w && source_h == s.h);
        if (copy_rows_direct) {
            const size_t row_bytes = static_cast<size_t>(s.w) * NUM_CHANNELS;
            for (int row = 0; row < s.h; ++row) {
                size_t dest_pixels = 0;
                size_t dest_offset = 0;
                if (!checked_mul_size_t(static_cast<size_t>(s.y + row), static_cast<size_t>(atlas_width), dest_pixels)
                    || !checked_add_size_t(dest_pixels, static_cast<size_t>(s.x), dest_pixels)
                    || !checked_mul_size_t(dest_pixels, NUM_CHANNELS, dest_offset)
                    || dest_offset > atlas.size()
                    || row_bytes > atlas.size() - dest_offset) {
                    error_out = "Atlas indexing out of bounds: " + s.path;
                    stbi_image_free(data);
                    return false;
                }

                size_t src_pixels = 0;
                size_t src_offset = 0;
                if (!checked_mul_size_t(static_cast<size_t>(source_y + row), static_cast<size_t>(w), src_pixels)
                    || !checked_add_size_t(src_pixels, static_cast<size_t>(source_x), src_pixels)
                    || !checked_mul_size_t(src_pixels, NUM_CHANNELS, src_offset)
                    || src_offset > source_bytes
                    || row_bytes > source_bytes - src_offset) {
                    error_out = "Source indexing overflow: " + s.path;
                    stbi_image_free(data);
                    return false;
                }
                std::memcpy(atlas.data() + dest_offset, data + src_offset, row_bytes);
            }
        } else {
            for (int row = 0; row < s.h; ++row) {
                int sample_y = source_y + ((row * source_h) / s.h);
                for (int col = 0; col < s.w; ++col) {
                    int sample_x = source_x + ((col * source_w) / s.w);

                    size_t dest_pixels = 0;
                    size_t dest_offset = 0;
                    if (!checked_mul_size_t(static_cast<size_t>(s.y + row), static_cast<size_t>(atlas_width), dest_pixels)) {
                        error_out = "Atlas indexing overflow: " + s.path;
                        stbi_image_free(data);
                        return false;
                    }
                    dest_pixels += static_cast<size_t>(s.x + col);
                    if (!checked_mul_size_t(dest_pixels, NUM_CHANNELS, dest_offset)
                        || dest_offset > atlas.size()
                        || NUM_CHANNELS > atlas.size() - dest_offset) {
                        error_out = "Atlas indexing out of bounds: " + s.path;
                        stbi_image_free(data);
                        return false;
                    }

                    size_t src_pixels = 0;
                    size_t src_offset = 0;
                    if (!checked_mul_size_t(static_cast<size_t>(sample_y), static_cast<size_t>(w), src_pixels)
                        || !checked_add_size_t(src_pixels, static_cast<size_t>(sample_x), src_pixels)
                        || !checked_mul_size_t(src_pixels, NUM_CHANNELS, src_offset)
                        || src_offset > source_bytes
                        || NUM_CHANNELS > source_bytes - src_offset) {
                        error_out = "Source indexing overflow: " + s.path;
                        stbi_image_free(data);
                        return false;
                    }

                    atlas[dest_offset + CHANNEL_R] = data[src_offset + CHANNEL_R];
                    atlas[dest_offset + CHANNEL_G] = data[src_offset + CHANNEL_G];
                    atlas[dest_offset + CHANNEL_B] = data[src_offset + CHANNEL_B];
                    atlas[dest_offset + CHANNEL_A] = data[src_offset + CHANNEL_A];
                }
            }
        }

        stbi_image_free(data);
        return true;
    };

    unsigned int worker_count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
    if (worker_count == 0) {
        worker_count = 1;
    }
    worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(std::max<size_t>(1, sprites.size())));

    const bool can_parallel_blit = (worker_count > 1) && !sprites_have_overlap(sprites);
    if (!can_parallel_blit) {
        for (const auto& s : sprites) {
            std::string error;
            if (!blit_sprite(s, error)) {
                std::cerr << error << "\n";
                return 1;
            }
        }
    } else {
        std::atomic<size_t> next_index{0};
        std::atomic<bool> failed{false};
        std::mutex error_mutex;
        std::string first_error;
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (unsigned int i = 0; i < worker_count; ++i) {
            workers.emplace_back([&]() {
                while (!failed.load(std::memory_order_relaxed)) {
                    size_t idx = next_index.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= sprites.size()) {
                        break;
                    }
                    std::string error;
                    if (!blit_sprite(sprites[idx], error)) {
                        {
                            std::scoped_lock lock(error_mutex);
                            if (first_error.empty()) {
                                first_error = std::move(error);
                            }
                        }
                        failed.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        if (failed.load(std::memory_order_relaxed)) {
            std::cerr << (first_error.empty() ? "Failed to process sprite" : first_error) << "\n";
            return 1;
        }
    }

    if (draw_frame_lines) {
        for (const auto& s : sprites) {
            draw_sprite_outline(atlas, atlas_width, atlas_height, s, line_width, line_color);
        }
    }

#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "Failed to set stdout to binary mode\n";
        return 1;
    }
#endif

    auto write_callback = [](void* context, void* data, int size) {
        auto* out = static_cast<std::ostream*>(context);
        out->write(static_cast<char*>(data), size);
    };

    if (stbi_write_png_to_func(write_callback, &std::cout,
                                atlas_width, atlas_height, static_cast<int>(NUM_CHANNELS),
                                atlas.data(), atlas_width * static_cast<int>(NUM_CHANNELS)) == 0) {
        std::cerr << "Failed to write PNG\n";
        return 1;
    }

    return 0;
}
