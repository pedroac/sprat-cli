// spratframes.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 src/spratframes.cpp -o spratframes

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <fcntl.h>
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
#include <utility>
#include <ranges>
#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include <filesystem>
namespace fs = std::filesystem;
#include <charconv>
#include <cctype>
#include <limits>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <thread>
#include "core/cli_parse.h"
#include "core/i18n.h"

#include <stb_image.h>

// Configuration
constexpr int k_default_rectangle_color_r = 255;
constexpr int k_default_rectangle_color_g = 0;
constexpr int k_default_rectangle_color_b = 255;
constexpr int k_default_tolerance = 1; // Standardized to match other tools
constexpr int k_default_min_sprite_size = 4;
constexpr int k_default_max_sprites = 10000;
constexpr int k_default_threads = 0; // Auto-detect
constexpr unsigned char k_max_alpha = 255;
constexpr unsigned char k_min_alpha = 0;
constexpr int k_max_image_dimension = 32768;
constexpr size_t k_max_total_pixels = 100000000;
constexpr int k_default_color_distance_threshold = 30;
constexpr int k_strict_color_distance_threshold = 15;
constexpr int k_hex_short_len = 3;
constexpr int k_hex_long_len = 6;
constexpr int k_hex_base = 16;
constexpr int k_rgb_prefix_len = 4;
constexpr int k_rgb_suffix_len = 1;
constexpr int k_max_channel_value = 255;

namespace {
using sprat::core::parse_positive_int;
using sprat::core::parse_non_negative_int;
using sprat::core::tr;
using sprat::core::to_quoted;
using sprat::core::trim_copy;

struct Color {
    unsigned char r, g, b, a;
    
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    bool operator!=(const Color& other) const {
        return !(*this == other);
    }
    
    [[nodiscard]] bool is_opaque() const {
        return a == k_max_alpha;
    }
    
    [[nodiscard]] bool is_transparent() const {
        return a == k_min_alpha;
    }
};

struct Rectangle {
    int x, y, w, h;
    
    [[nodiscard]] int right() const { return x + w; }
    [[nodiscard]] int bottom() const { return y + h; }
    
    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    
    [[nodiscard]] bool intersects(const Rectangle& other) const {
        return x < other.right() && right() > other.x
                 && y < other.bottom() && bottom() > other.y;
    }
    
    [[nodiscard]] int area() const {
        return w * h;
    }
};

struct SpriteFrame {
    Rectangle bounds;
    int index;
};

struct FramesConfig {
    fs::path input_path;
    bool has_rectangles = false;
    Color rectangle_color{.r=k_default_rectangle_color_r, .g=k_default_rectangle_color_g, .b=k_default_rectangle_color_b, .a=k_max_alpha};
    int tolerance = k_default_tolerance;
    int min_sprite_size = k_default_min_sprite_size;
    int max_sprites = k_default_max_sprites;
    unsigned int threads = k_default_threads;
    bool force = false;
};

class SpriteFramesDetector {
private:
    FramesConfig config_;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<unsigned char> image_data_;
    
    // Connected component analysis
    std::vector<int> component_labels_;
    std::vector<Rectangle> component_bounds_;
    std::vector<int> component_sizes_;

    // Reusable flood fill stack
    std::vector<std::pair<int, int>> fill_stack_;
    
    // Rectangle detection
    std::vector<Rectangle> detected_rectangles_;
    
    bool has_detected_background_ = false;
    Color detected_background_color_{0, 0, 0, 0};
    
public:
    SpriteFramesDetector(FramesConfig  config) : config_(std::move(config)) {}
    

    bool load_image() {
        // Validate image dimensions to prevent integer overflow
        int req_channels = 4; // Always load with alpha
        unsigned char* data = stbi_load(config_.input_path.string().c_str(), &width_, &height_, &channels_, req_channels);
        
        if (data == nullptr) {
            std::cerr << tr("Error: Failed to load image: ") << to_quoted(config_.input_path.string())
                      << tr(" (Reason: ") << stbi_failure_reason() << tr(")") << '\n';
            return false;
        }
        
        // Validate dimensions are reasonable
        if (width_ <= 0 || height_ <= 0 || width_ > k_max_image_dimension || height_ > k_max_image_dimension) {
            std::cerr << tr("Error: Invalid image dimensions: ") << width_ << "x" << height_ << '\n';
            stbi_image_free(data);
            return false;
        }
        
        // Check for potential overflow in size calculation
        size_t total_pixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        if (total_pixels > k_max_total_pixels) { // Limit to ~100MP
            std::cerr << tr("Error: Image too large: ") << total_pixels << tr(" pixels") << '\n';
            stbi_image_free(data);
            return false;
        }
        
        image_data_.assign(data, data + (total_pixels * 4));
        stbi_image_free(data);
        
        return true;
    }
    
    bool detect_frames() {
        if (!load_image()) {
            return false;
        }
        
        // Preprocess image: check first pixel and make it transparent if not already
        if (!preprocess_image()) {
            std::cerr << tr("Error: Failed to preprocess image") << '\n';
            return false;
        }
        
        std::vector<SpriteFrame> frames;
        
        if (config_.has_rectangles) {
            if (!detect_rectangles()) {
                std::cerr << tr("Error: Failed to detect rectangles") << '\n';
                return false;
            }
            frames = extract_from_rectangles();
        } else {
            if (!find_connected_components()) {
                std::cerr << tr("Error: Failed to find connected components") << '\n';
                return false;
            }
            frames = extract_from_components();
        }
        
        if (frames.empty()) {
            std::cerr << tr("Warning: No frames found") << '\n';
            return true;
        }
        
        return output_frames(frames);
    }
    
private:
    // Frames are the regions enclosed by rectangle-colored lines: 4-connected areas of other
    // pixels that do not reach the image edge (the outside of the boxes does). Taking the
    // enclosed area instead of the outline's bounding box handles separate boxes, grids
    // whose cells share border lines, and borders thicker than one pixel alike. The whole
    // cell is the frame, keeping the sheet's alignment; cells with nothing visible in them
    // (empty grid slots) are skipped.
    bool detect_rectangles() {
        detected_rectangles_.clear();

        const size_t w = static_cast<size_t>(width_);
        const size_t h = static_cast<size_t>(height_);
        std::vector<std::uint8_t> visited(w * h, 0);
        std::vector<size_t> stack;

        for (size_t start = 0; start < w * h; ++start) {
            if (visited[start] != 0U || is_rectangle_pixel(start)) {
                continue;
            }
            visited[start] = 1;
            stack.assign(1, start);
            size_t min_x = start % w;
            size_t max_x = min_x;
            size_t min_y = start / w;
            size_t max_y = min_y;
            bool touches_edge = false;
            bool has_content = false;
            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                has_content = has_content || is_content_pixel(i);
                const size_t x = i % w;
                const size_t y = i / w;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
                touches_edge = touches_edge || x == 0 || y == 0 || x + 1 == w || y + 1 == h;
                const auto visit = [&](size_t n) {
                    if (visited[n] == 0U && !is_rectangle_pixel(n)) {
                        visited[n] = 1;
                        stack.push_back(n);
                    }
                };
                if (x > 0) visit(i - 1);
                if (x + 1 < w) visit(i + 1);
                if (y > 0) visit(i - w);
                if (y + 1 < h) visit(i + w);
            }
            if (touches_edge || !has_content) {
                continue;
            }
            const Rectangle cell{.x = static_cast<int>(min_x), .y = static_cast<int>(min_y),
                                 .w = static_cast<int>(max_x - min_x + 1), .h = static_cast<int>(max_y - min_y + 1)};
            if (cell.area() >= config_.min_sprite_size) {
                detected_rectangles_.push_back(cell);
            }
        }

        // A closed shape of the rectangle color inside a sprite encloses a region of its own;
        // merging folds it back into the cell around it.
        merge_rectangles(detected_rectangles_);

        return !detected_rectangles_.empty();
    }

    // Visible and not the sheet's background. Background enclosed by rectangle lines is not
    // connected to the image edge, so preprocessing leaves it opaque; it is checked by color.
    [[nodiscard]] bool is_content_pixel(size_t pixel_index) const {
        const Color c = pixel_at(pixel_index);
        return !c.is_transparent() &&
               !(has_detected_background_ &&
                 color_distance(c, detected_background_color_) <= k_strict_color_distance_threshold);
    }

    [[nodiscard]] bool is_rectangle_pixel(size_t pixel_index) const {
        // Matches the rectangle color with some tolerance for anti-aliasing.
        return color_distance(pixel_at(pixel_index), config_.rectangle_color) < k_default_color_distance_threshold;
    }

    [[nodiscard]] static int color_distance(const Color& a, const Color& b) {
        return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r))
               + std::abs(static_cast<int>(a.g) - static_cast<int>(b.g))
               + std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
    }

    // Pieces of one sprite (a detached weapon, a closed loop of the rectangle color inside a
    // grid cell) sit mostly inside its box, while neighbouring sprites in a packed sheet only
    // clip each other's corners. So two boxes are one sprite when their overlap covers at
    // least half of the smaller box; boxes that merely clip stay separate frames. Scattered
    // particles that never overlap their effect's box are joined with --tolerance instead.
    [[nodiscard]] static bool same_sprite(const Rectangle& a, const Rectangle& b) {
        if (!a.intersects(b)) {
            return false;
        }
        const long long overlap_w = std::min(a.right(), b.right()) - std::max(a.x, b.x);
        const long long overlap_h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
        const long long smaller = std::min(static_cast<long long>(a.w) * a.h, static_cast<long long>(b.w) * b.h);
        return 2 * overlap_w * overlap_h >= smaller;
    }

    // Merges boxes of the same sprite. A merged box can cover more pieces than either part
    // did, so passes repeat until nothing changes.
    static void merge_rectangles(std::vector<Rectangle>& rects) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < rects.size(); ++i) {
                for (size_t j = i + 1; j < rects.size();) {
                    if (!same_sprite(rects[i], rects[j])) {
                        ++j;
                        continue;
                    }
                    const int left = std::min(rects[i].x, rects[j].x);
                    const int top = std::min(rects[i].y, rects[j].y);
                    const int right = std::max(rects[i].right(), rects[j].right());
                    const int bottom = std::max(rects[i].bottom(), rects[j].bottom());
                    rects[i] = {.x = left, .y = top, .w = right - left, .h = bottom - top};
                    rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
                    changed = true;
                    j = i + 1;  // the grown box may now cover earlier candidates
                }
            }
        }
    }

    std::vector<SpriteFrame> extract_from_rectangles() {
        sort_reading_order(detected_rectangles_);
        if (std::cmp_greater(detected_rectangles_.size(), config_.max_sprites)) {
            std::cerr << tr("Warning: Found ") << detected_rectangles_.size() << tr(" frames; keeping the first ")
                      << config_.max_sprites << tr(" (see --max-sprites)") << '\n';
            detected_rectangles_.resize(static_cast<size_t>(config_.max_sprites));
        }

        std::vector<SpriteFrame> frames;
        frames.reserve(detected_rectangles_.size());
        for (size_t i = 0; i < detected_rectangles_.size(); ++i) {
            frames.push_back({.bounds = detected_rectangles_[i], .index = static_cast<int>(i)});
        }
        return frames;
    }

    bool find_connected_components() {
        component_labels_.assign(static_cast<size_t>(width_) * height_, -1);
        component_bounds_.clear();
        component_sizes_.clear();
        
        int component_id = 0;
        
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (component_labels_[(static_cast<size_t>(y) * width_) + x] == -1 && is_sprite_pixel(x, y)) {
                    Rectangle bounds{};
                    int size = flood_fill_component(x, y, component_id, bounds);
                    
                    if (size >= config_.min_sprite_size) {
                        component_bounds_.push_back(bounds);
                        component_sizes_.push_back(size);
                        component_id++;
                    }
                }
            }
        }
        
        // Fold the pieces of one sprite into a single frame
        merge_rectangles(component_bounds_);
        
        // Return true if the algorithm completed successfully, even if no components were found
        // This allows the extraction to proceed with 0 sprites when all are filtered by min-size
        return true;
    }
    
    [[nodiscard]] bool is_sprite_pixel(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }
        
        size_t idx = (static_cast<size_t>(y) * width_ + x) * 4;
        Color pixel{.r=image_data_.at(idx), .g=image_data_.at(idx+1), .b=image_data_.at(idx+2), .a=image_data_.at(idx+3)};
        
        // Consider pixel as part of sprite if it's not fully transparent
        // and not the rectangle color (if rectangles are present)
        if (pixel.is_transparent()) {
            return false;
        }
        
        if (config_.has_rectangles && color_distance(pixel, config_.rectangle_color) < k_default_color_distance_threshold) {
            return false;
        }
        
        return true;
    }
    
    int flood_fill_component(int start_x, int start_y, int component_id, Rectangle& bounds) {
        fill_stack_.clear();
        fill_stack_.emplace_back(start_x, start_y);
        component_labels_.at((static_cast<size_t>(start_y) * width_) + start_x) = component_id;

        int min_x = start_x;
        int max_x = start_x;
        int min_y = start_y;
        int max_y = start_y;
        int size = 0;

        constexpr std::array<int, 8> dx = {-1, 1, 0, 0, -1, 1, -1, 1};
        constexpr std::array<int, 8> dy = {0, 0, -1, 1, -1, -1, 1, 1};

        while (!fill_stack_.empty()) {
            auto [x, y] = fill_stack_.back();
            fill_stack_.pop_back();

            // Transparent pixels near the sprite (--tolerance) only bridge gaps between its
            // pieces; they are not part of the frame's bounds or its size.
            if (is_sprite_pixel(x, y)) {
                size++;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }

            for (size_t i = 0; i < dx.size(); ++i) {
                int nx = x + dx[i];
                int ny = y + dy[i];

                if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_
                    && component_labels_.at((static_cast<size_t>(ny) * width_) + nx) == -1
                    && (is_sprite_pixel(nx, ny) || is_near_sprite_pixel(nx, ny))) {
                    component_labels_.at((static_cast<size_t>(ny) * width_) + nx) = component_id;
                    fill_stack_.emplace_back(nx, ny);
                }
            }
        }

        bounds.x = min_x;
        bounds.y = min_y;
        bounds.w = max_x - min_x + 1;
        bounds.h = max_y - min_y + 1;

        return size;
    }
    
    [[nodiscard]] bool is_near_sprite_pixel(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }
        
        // Check if this pixel is within tolerance distance of any sprite pixel
        // Tolerance defines minimum distance between frames
        // Tolerance=1 means at least 1 transparent pixel between colored pixels
        // This means sprites separated by 1 transparent pixel should be separate
        for (int dy = -config_.tolerance; dy <= config_.tolerance; ++dy) {
            for (int dx = -config_.tolerance; dx <= config_.tolerance; ++dx) {
                int nx = x + dx;
                int ny = y + dy;
                
                if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
                    if (is_sprite_pixel(nx, ny)) {
                        // Calculate Manhattan distance (number of transparent pixels between)
                        int distance = std::abs(dx) + std::abs(dy);
                        // Connect sprites if distance is less than tolerance
                        // This means tolerance=1 allows connection through 0 transparent pixels
                        // tolerance=2 allows connection through 1 transparent pixel
                        if (distance < config_.tolerance) {
                            return true;
                        }
                    }
                }
            }
        }
        
        return false;
    }
    
    std::vector<SpriteFrame> extract_from_components() {
        std::vector<Rectangle> bounds = component_bounds_;
        // Over the limit, keep the largest frames (stray specks go first).
        if (std::cmp_greater(bounds.size(), config_.max_sprites)) {
            std::cerr << tr("Warning: Found ") << bounds.size() << tr(" frames; keeping the ")
                      << config_.max_sprites << tr(" largest (see --max-sprites)") << '\n';
            std::ranges::stable_sort(bounds, std::ranges::greater{}, &Rectangle::area);
            bounds.resize(static_cast<size_t>(config_.max_sprites));
        }
        sort_reading_order(bounds);

        std::vector<SpriteFrame> frames;
        frames.reserve(bounds.size());
        for (size_t i = 0; i < bounds.size(); ++i) {
            frames.push_back({.bounds = bounds[i], .index = static_cast<int>(i)});
        }
        return frames;
    }

    // Frames are numbered like text is read, so animation strips and grids keep their frame
    // order: rows top to bottom, left to right within a row. A row is started by its
    // topmost frame and holds every frame whose top edge lies above the row's shortest
    // bottom edge (it overlaps the band all frames of the row share). That tolerates frames
    // of different sizes or baselines in one row, and keeps one tall frame from swallowing
    // the rows below it.
    static void sort_reading_order(std::vector<Rectangle>& rects) {
        std::ranges::stable_sort(rects, [](const Rectangle& a, const Rectangle& b) {
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        std::vector<Rectangle> ordered;
        ordered.reserve(rects.size());
        std::vector<bool> placed(rects.size(), false);
        for (size_t i = 0; i < rects.size(); ++i) {
            if (placed[i]) {
                continue;
            }
            long long row_bottom = static_cast<long long>(rects[i].y) + rects[i].h;
            std::vector<Rectangle> row;
            // Frames are sorted by top edge, so every frame seen here starts below the row's top.
            for (size_t j = i; j < rects.size(); ++j) {
                const long long bottom = static_cast<long long>(rects[j].y) + rects[j].h;
                if (!placed[j] && rects[j].y < row_bottom) {
                    placed[j] = true;
                    row.push_back(rects[j]);
                    row_bottom = std::min(row_bottom, bottom);
                }
            }
            std::ranges::stable_sort(row, {}, &Rectangle::x);
            ordered.insert(ordered.end(), row.begin(), row.end());
        }
        rects = std::move(ordered);
    }
    
    bool output_frames(const std::vector<SpriteFrame>& frames) {
        // Default to spratframes format (minimalist)
        return output_spratframes(frames);
    }
    
    [[nodiscard]] static std::string color_to_hex(const Color& c) {
        auto to_hex_digit = [](int v) -> char {
            return static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10));
        };
        std::string s;
        s += to_hex_digit(c.r >> 4);
        s += to_hex_digit(c.r & 0xf);
        s += to_hex_digit(c.g >> 4);
        s += to_hex_digit(c.g & 0xf);
        s += to_hex_digit(c.b >> 4);
        s += to_hex_digit(c.b & 0xf);
        return s;
    }
    
    [[nodiscard]] bool output_spratframes(const std::vector<SpriteFrame>& frames) const {
        // First line: path <filepath>
        std::cout << "path " << to_quoted(config_.input_path.string()) << "\n";
        
        // Check if we need to output background color
        if (config_.has_rectangles) {
            std::cout << "background " << color_to_hex(config_.rectangle_color) << "\n";
        } else if (has_detected_background_) {
            std::cout << "background " << color_to_hex(detected_background_color_) << "\n";
        }
        
        // Each frame: sprite x,y w,h
        for (const auto& frame : frames) {
            std::cout << "sprite " << frame.bounds.x << "," << frame.bounds.y 
                << " " << frame.bounds.w << "," << frame.bounds.h << "\n";
        }
        
        return true;
    }
    
    [[nodiscard]] Color pixel_at(size_t pixel_index) const {
        const size_t idx = pixel_index * 4;
        return {.r = image_data_[idx], .g = image_data_[idx + 1], .b = image_data_[idx + 2], .a = image_data_[idx + 3]};
    }

    // A solid background exists when all four corners share one opaque color. Only pixels of
    // that color connected to the image edge are made transparent, so sprite pixels that
    // happen to match it (an eye, an outline inside the sprite) are kept. A sprite touching
    // a corner, as in tightly packed atlases, is therefore never taken for the background.
    bool preprocess_image() {
        if (width_ <= 0 || height_ <= 0) {
            return false;
        }

        const size_t w = static_cast<size_t>(width_);
        const size_t h = static_cast<size_t>(height_);
        const Color corner = pixel_at(0);
        const std::array<size_t, 3> other_corners = {w - 1, (h - 1) * w, (h * w) - 1};
        if (corner.is_transparent() ||
            std::ranges::any_of(other_corners, [&](size_t i) {
                const Color c = pixel_at(i);
                return c.is_transparent() || color_distance(c, corner) > k_strict_color_distance_threshold;
            })) {
            return true;
        }
        has_detected_background_ = true;
        detected_background_color_ = corner;

        const auto is_background = [&](size_t i) {
            const Color c = pixel_at(i);
            return !c.is_transparent() && color_distance(c, corner) <= k_strict_color_distance_threshold;
        };
        std::vector<std::uint8_t> visited(w * h, 0);
        std::vector<size_t> stack;
        const auto push = [&](size_t i) {
            if (visited[i] == 0U && is_background(i)) {
                visited[i] = 1;
                stack.push_back(i);
            }
        };
        for (size_t x = 0; x < w; ++x) {
            push(x);
            push(((h - 1) * w) + x);
        }
        for (size_t y = 0; y < h; ++y) {
            push(y * w);
            push((y * w) + w - 1);
        }
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            image_data_[(i * 4) + 3] = 0;
            const size_t x = i % w;
            const size_t y = i / w;
            if (x > 0) push(i - 1);
            if (x + 1 < w) push(i + 1);
            if (y > 0) push(i - w);
            if (y + 1 < h) push(i + w);
        }
        return true;
    }
};

// Utility functions
bool parse_color(const std::string& value, Color& out) {
    if (value.empty()) { return false;
}

    auto parse_byte = [](const std::string& token, int& out_value) -> bool {
        if (!parse_non_negative_int(token, out_value)) {
            return false;
        }
        return out_value >= 0 && out_value <= k_max_channel_value;
    };

    auto parse_hex_byte = [](const std::string& token, int& out_value) -> bool {
        if (token.empty()) {
            return false;
        }
        const auto [ptr, ec] = std::from_chars(
            token.data(), token.data() + token.size(), out_value, k_hex_base);
        if (ec != std::errc() || ptr != token.data() + token.size()) {
            return false;
        }
        return out_value >= 0 && out_value <= k_max_channel_value;
    };
    
    std::string lower = value;
    std::ranges::transform(lower, lower.begin(), ::tolower);
    
    // Handle hex colors
    if (lower.at(0) == '#') {
        std::string hex = lower.substr(1);
        if (hex.length() == k_hex_short_len) {
            // #RGB format
            int r = 0;
            int g = 0;
            int b = 0;
            if (!parse_hex_byte(hex.substr(0, 1), r)
                || !parse_hex_byte(hex.substr(1, 1), g)
                || !parse_hex_byte(hex.substr(2, 1), b)) {
                return false;
            }
            out.r = static_cast<unsigned char>((r << 4) | r);
            out.g = static_cast<unsigned char>((g << 4) | g);
            out.b = static_cast<unsigned char>((b << 4) | b);
            out.a = k_max_alpha;
            return true;
        } if (hex.length() == k_hex_long_len) {
            // #RRGGBB format
            int r = 0;
            int g = 0;
            int b = 0;
            if (!parse_hex_byte(hex.substr(0, 2), r)
                || !parse_hex_byte(hex.substr(2, 2), g)
                || !parse_hex_byte(hex.substr(4, 2), b)) {
                return false;
            }
            out.r = static_cast<unsigned char>(r);
            out.g = static_cast<unsigned char>(g);
            out.b = static_cast<unsigned char>(b);
            out.a = k_max_alpha;
            return true;
        }
        return false;
    }
    
    // Handle RGB(r,g,b) format
    if (lower.starts_with("rgb(") && lower.back() == ')') {
        std::string content = trim_copy(lower.substr(k_rgb_prefix_len, lower.length() - (k_rgb_prefix_len + k_rgb_suffix_len)));
        std::istringstream iss(content);
        std::string r_str;
        std::string g_str;
        std::string b_str;
        
        if (std::getline(iss, r_str, ',')
            && std::getline(iss, g_str, ',')
            && std::getline(iss, b_str)) {
            int r = 0;
            int g = 0;
            int b = 0;
            if (!parse_byte(trim_copy(r_str), r)
                || !parse_byte(trim_copy(g_str), g)
                || !parse_byte(trim_copy(b_str), b)) {
                return false;
            }
            out.r = static_cast<unsigned char>(r);
            out.g = static_cast<unsigned char>(g);
            out.b = static_cast<unsigned char>(b);
            out.a = static_cast<unsigned char>(k_max_channel_value);
            return true;
        }
        return false;
    }
    
    // Handle comma-separated values
    std::istringstream iss(value);
    std::string r_str;
    std::string g_str;
    std::string b_str;
    if (std::getline(iss, r_str, ',')
        && std::getline(iss, g_str, ',')
        && std::getline(iss, b_str)) {
        int r = 0;
        int g = 0;
        int b = 0;
        if (!parse_byte(trim_copy(r_str), r)
            || !parse_byte(trim_copy(g_str), g)
            || !parse_byte(trim_copy(b_str), b)) {
            return false;
        }
        out.r = static_cast<unsigned char>(r);
        out.g = static_cast<unsigned char>(g);
        out.b = static_cast<unsigned char>(b);
        out.a = static_cast<unsigned char>(k_max_channel_value);
        return true;
    }
    
    return false;
}

void print_usage() {
    std::cout << tr("Usage: spratframes [OPTIONS] <input_image>\n\n")
        << tr("Detect sprite frame rectangles in spritesheets.\n\n")
        << tr("Output format:\n")
        << tr("  SpratFrames format: path <input>, then sprite x,y w,h\n\n")
        << tr("Options:\n")
        << tr("  --has-rectangles          Spritesheet has rectangles surrounding sprites\n")
        << tr("  --rectangle-color COLOR   Color of rectangle borders (default: ")
        << k_default_rectangle_color_r << "," << k_default_rectangle_color_g << "," << k_default_rectangle_color_b << ")\n"
        << tr("                            Formats: #RRGGBB, #RGB, RGB(r,g,b), r,g,b\n")
        << tr("  --tolerance N            Distance tolerance for sprite grouping (default: ") << k_default_tolerance << ")\n"
        << tr("  --min-size N             Minimum sprite size in pixels (default: ") << k_default_min_sprite_size << ")\n"
        << tr("  --max-sprites N          Maximum number of sprites to extract (default: ") << k_default_max_sprites << ")\n"
        << tr("  --threads N              Number of threads to use (default: ") << k_default_threads << tr(" = auto)\n")
        << tr("  --help, -h               Show this help message\n")
        << tr("  --version, -v            Show version\n\n")
        << tr("Examples:\n")
        << tr("  spratframes sheet.png\n")
        << tr("  spratframes --has-rectangles --rectangle-color=\"#FF00FF\" sheet.png\n")
        << tr("  spratframes --tolerance 2 --min-size 8 sheet.png\n")
        << tr("  spratframes sheet.png > frames.spratframes\n");
}
   
} // namespace

int run_spratframes(int argc, char** argv) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << tr("Failed to set stdout to binary mode\n");
    }
#endif
    FramesConfig config;
    bool show_help = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            show_help = true;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << tr("spratframes version ") << SPRAT_VERSION << "\n";
            return 0;
        } else if (arg == "--has-rectangles") {
            config.has_rectangles = true;
        } else if (arg == "--rectangle-color" && i + 1 < argc) {
            if (!parse_color(argv[++i], config.rectangle_color)) {
                std::cerr << tr("Error: Invalid color format: ") << argv[i] << '\n';
                return 1;
            }
        } else if (arg == "--tolerance" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.tolerance)) {
                std::cerr << tr("Error: Invalid tolerance value: ") << argv[i] << '\n';
                return 1;
            }
        } else if (arg == "--min-size" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.min_sprite_size)) {
                std::cerr << tr("Error: Invalid min-size value: ") << argv[i] << '\n';
                return 1;
            }
        } else if (arg == "--max-sprites" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.max_sprites)) {
                std::cerr << tr("Error: Invalid max-sprites value: ") << argv[i] << '\n';
                return 1;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            int threads_int = 0;
            if (!parse_positive_int(argv[++i], threads_int)) {
                std::cerr << tr("Error: Invalid threads value: ") << argv[i] << '\n';
                return 1;
            }
            config.threads = static_cast<unsigned int>(threads_int);
        } else if (arg.empty() || arg[0] == '-') {
            std::cerr << tr("Error: Unknown option: ") << arg << '\n';
            print_usage();
            return 1;
        } else {
            // Positional arguments
            if (config.input_path.empty()) {
                config.input_path = argv[i];
            } else {
                std::cerr << tr("Error: Too many arguments") << '\n';
                print_usage();
                return 1;
            }
        }
    }
    
    if (show_help) {
        print_usage();
        return 0;
    }
    
    // Validate required arguments
    if (config.input_path.empty()) {
        std::cerr << tr("Error: Input image path is required") << '\n';
        print_usage();
        return 1;
    }
    
    // Validate input file exists
    if (!fs::exists(config.input_path) || !fs::is_regular_file(config.input_path)) {
        std::cerr << tr("Error: Input file does not exist or is not a file: ") << to_quoted(config.input_path.string()) << '\n';
        return 1;
    }
    
    // Set default threads if not specified
    if (config.threads == 0) {
        config.threads = std::max(1U, std::thread::hardware_concurrency());
    }
    
    // Detect frames
    SpriteFramesDetector detector(config);
    if (!detector.detect_frames()) {
        std::cerr << tr("Error: Failed to detect frames") << '\n';
        return 1;
    }
    
    return 0;
}
