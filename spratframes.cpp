// spratframes.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 spratframes.cpp -o spratframes

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cmath>
#include <utility>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <thread>
#include <queue>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

// Configuration
constexpr int k_default_rectangle_color_r = 255;
constexpr int k_default_rectangle_color_g = 0;
constexpr int k_default_rectangle_color_b = 255;
constexpr int k_default_tolerance = 1; // Standardized to match other tools
constexpr int k_default_min_sprite_size = 4;
constexpr int k_default_max_sprites = 10000;
constexpr int k_default_threads = 0; // Auto-detect

struct Color {
    unsigned char r, g, b, a;
    
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    bool operator!=(const Color& other) const {
        return !(*this == other);
    }
    
    bool is_opaque() const {
        return a == 255;
    }
    
    bool is_transparent() const {
        return a == 0;
    }
};

struct Rectangle {
    int x, y, w, h;
    
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    
    bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    
    bool intersects(const Rectangle& other) const {
        return !(x >= other.right() || right() <= other.x || 
                 y >= other.bottom() || bottom() <= other.y);
    }
    
    int area() const {
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
    Color rectangle_color{k_default_rectangle_color_r, k_default_rectangle_color_g, k_default_rectangle_color_b, 255};
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
    
    // Rectangle detection
    std::vector<Rectangle> detected_rectangles_;
    
public:
    SpriteFramesDetector(const FramesConfig& config) : config_(config) {}
    
    bool load_image() {
        // Validate image dimensions to prevent integer overflow
        int req_channels = 4; // Always load with alpha
        unsigned char* data = stbi_load(config_.input_path.string().c_str(), &width_, &height_, &channels_, req_channels);
        
        if (!data) {
            std::cerr << "Error: Failed to load image: " << config_.input_path << std::endl;
            return false;
        }
        
        // Validate dimensions are reasonable
        if (width_ <= 0 || height_ <= 0 || width_ > 32768 || height_ > 32768) {
            std::cerr << "Error: Invalid image dimensions: " << width_ << "x" << height_ << std::endl;
            stbi_image_free(data);
            return false;
        }
        
        // Check for potential overflow in size calculation
        size_t total_pixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        if (total_pixels > 100000000) { // Limit to ~100MP
            std::cerr << "Error: Image too large: " << total_pixels << " pixels" << std::endl;
            stbi_image_free(data);
            return false;
        }
        
        image_data_.assign(data, data + total_pixels * 4);
        stbi_image_free(data);
        
        return true;
    }
    
    bool detect_frames() {
        if (!load_image()) {
            return false;
        }
        
        // Preprocess image: check first pixel and make it transparent if not already
        if (!preprocess_image()) {
            std::cerr << "Error: Failed to preprocess image" << std::endl;
            return false;
        }
        
        std::vector<SpriteFrame> frames;
        
        if (config_.has_rectangles) {
            if (!detect_rectangles()) {
                std::cerr << "Error: Failed to detect rectangles" << std::endl;
                return false;
            }
            frames = extract_from_rectangles();
        } else {
            if (!find_connected_components()) {
                std::cerr << "Error: Failed to find connected components" << std::endl;
                return false;
            }
            frames = extract_from_components();
        }
        
        if (frames.empty()) {
            std::cerr << "Warning: No frames found" << std::endl;
            return true;
        }
        
        return output_frames(frames);
    }
    
private:
    bool detect_rectangles() {
        detected_rectangles_.clear();
        
        std::vector<uint8_t> visited(width_ * height_, 0);
        
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (!visited[y * width_ + x] && is_rectangle_pixel(x, y)) {
                    Rectangle rect = flood_fill_rectangle(x, y, visited);
                    if (rect.w > 0 && rect.h > 0 && rect.area() >= config_.min_sprite_size) {
                        detected_rectangles_.push_back(rect);
                    }
                }
            }
        }
        
        // Merge overlapping rectangles
        merge_rectangles(detected_rectangles_);
        
        return !detected_rectangles_.empty();
    }
    
    bool is_rectangle_pixel(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }
        
        size_t idx = (y * width_ + x) * 4;
        Color pixel{image_data_[idx], image_data_[idx+1], image_data_[idx+2], image_data_[idx+3]};
        
        // Check if pixel matches rectangle color (with some tolerance for anti-aliasing)
        return color_distance(pixel, config_.rectangle_color) < 30;
    }
    
    int color_distance(const Color& a, const Color& b) const {
        return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) +
               std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) +
               std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
    }
    
    Rectangle flood_fill_rectangle(int start_x, int start_y, std::vector<uint8_t>& visited) {
        Rectangle bounds{start_x, start_y, 1, 1};
        std::queue<std::pair<int, int>> queue;
        queue.push({start_x, start_y});
        visited[start_y * width_ + start_x] = 1;
        
        int min_x = start_x, max_x = start_x;
        int min_y = start_y, max_y = start_y;
        
        const int dx[] = {-1, 1, 0, 0};
        const int dy[] = {0, 0, -1, 1};
        
        while (!queue.empty()) {
            auto [x, y] = queue.front();
            queue.pop();
            
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            
            for (int i = 0; i < 4; ++i) {
                int nx = x + dx[i];
                int ny = y + dy[i];
                
                if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_ && 
                    !visited[ny * width_ + nx] && is_rectangle_pixel(nx, ny)) {
                    visited[ny * width_ + nx] = 1;
                    queue.push({nx, ny});
                }
            }
        }
        
        bounds.x = min_x;
        bounds.y = min_y;
        bounds.w = max_x - min_x + 1;
        bounds.h = max_y - min_y + 1;
        
        return bounds;
    }
    
    void merge_rectangles(std::vector<Rectangle>& rects) {
        if (rects.size() <= 1) return;
        
        std::vector<bool> merged(rects.size(), false);
        std::vector<Rectangle> result;
        
        for (size_t i = 0; i < rects.size(); ++i) {
            if (merged[i]) continue;
            
            Rectangle merged_rect = rects[i];
            merged[i] = true;
            
            bool changed = true;
            while (changed) {
                changed = false;
                for (size_t j = i + 1; j < rects.size(); ++j) {
                    if (merged[j]) continue;
                    
                    if (merged_rect.intersects(rects[j])) {
                        // Merge rectangles
                        int new_x = std::min(merged_rect.x, rects[j].x);
                        int new_y = std::min(merged_rect.y, rects[j].y);
                        int new_right = std::max(merged_rect.right(), rects[j].right());
                        int new_bottom = std::max(merged_rect.bottom(), rects[j].bottom());
                        
                        merged_rect.x = new_x;
                        merged_rect.y = new_y;
                        merged_rect.w = new_right - new_x;
                        merged_rect.h = new_bottom - new_y;
                        
                        merged[j] = true;
                        changed = true;
                    }
                }
            }
            
            result.push_back(merged_rect);
        }
        
        rects = std::move(result);
    }
    
    std::vector<SpriteFrame> extract_from_rectangles() {
        std::vector<SpriteFrame> frames;
        frames.reserve(detected_rectangles_.size());
        
        for (size_t i = 0; i < detected_rectangles_.size() && i < static_cast<size_t>(config_.max_sprites); ++i) {
            const Rectangle& rect = detected_rectangles_[i];
            
            // Extract sprite inside rectangle, removing the rectangle border
            Rectangle sprite_bounds = rect;
            sprite_bounds.x += 1;
            sprite_bounds.y += 1;
            sprite_bounds.w -= 2;
            sprite_bounds.h -= 2;
            
            // Ensure bounds are valid
            if (sprite_bounds.w <= 0 || sprite_bounds.h <= 0) continue;
            if (sprite_bounds.x < 0) { sprite_bounds.w += sprite_bounds.x; sprite_bounds.x = 0; }
            if (sprite_bounds.y < 0) { sprite_bounds.h += sprite_bounds.y; sprite_bounds.y = 0; }
            if (sprite_bounds.right() > width_) sprite_bounds.w = width_ - sprite_bounds.x;
            if (sprite_bounds.bottom() > height_) sprite_bounds.h = height_ - sprite_bounds.y;
            
            if (sprite_bounds.w > 0 && sprite_bounds.h > 0) {
                SpriteFrame frame;
                frame.bounds = sprite_bounds;
                frame.index = static_cast<int>(i);
                frames.push_back(frame);
            }
        }
        
        return frames;
    }
    
    bool find_connected_components() {
        component_labels_.assign(width_ * height_, -1);
        component_bounds_.clear();
        component_sizes_.clear();
        
        int component_id = 0;
        
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (component_labels_[y * width_ + x] == -1 && is_sprite_pixel(x, y)) {
                    Rectangle bounds;
                    int size = flood_fill_component(x, y, component_id, bounds);
                    
                    if (size >= config_.min_sprite_size) {
                        component_bounds_.push_back(bounds);
                        component_sizes_.push_back(size);
                        component_id++;
                    }
                }
            }
        }
        
        // Merge overlapping components to ensure frames never overlap
        merge_rectangles(component_bounds_);
        
        // Return true if the algorithm completed successfully, even if no components were found
        // This allows the extraction to proceed with 0 sprites when all are filtered by min-size
        return true;
    }
    
    bool is_sprite_pixel(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            return false;
        }
        
        size_t idx = (y * width_ + x) * 4;
        Color pixel{image_data_[idx], image_data_[idx+1], image_data_[idx+2], image_data_[idx+3]};
        
        // Consider pixel as part of sprite if it's not fully transparent
        // and not the rectangle color (if rectangles are present)
        if (pixel.is_transparent()) {
            return false;
        }
        
        if (config_.has_rectangles && color_distance(pixel, config_.rectangle_color) < 30) {
            return false;
        }
        
        return true;
    }
    
    int flood_fill_component(int start_x, int start_y, int component_id, Rectangle& bounds) {
        std::queue<std::pair<int, int>> queue;
        queue.push({start_x, start_y});
        component_labels_[start_y * width_ + start_x] = component_id;
        
        int min_x = start_x, max_x = start_x;
        int min_y = start_y, max_y = start_y;
        int size = 0;
        
        const int dx[] = {-1, 1, 0, 0, -1, 1, -1, 1};
        const int dy[] = {0, 0, -1, 1, -1, -1, 1, 1};
        
        while (!queue.empty()) {
            auto [x, y] = queue.front();
            queue.pop();
            size++;
            
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            
            for (int i = 0; i < 8; ++i) {
                int nx = x + dx[i];
                int ny = y + dy[i];
                
                if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_ && 
                    component_labels_[ny * width_ + nx] == -1 && is_sprite_pixel(nx, ny)) {
                    component_labels_[ny * width_ + nx] = component_id;
                    queue.push({nx, ny});
                } else if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_ && 
                           component_labels_[ny * width_ + nx] == -1 &&
                           is_near_sprite_pixel(nx, ny)) {
                    // Check if this pixel is within tolerance of a sprite pixel
                    component_labels_[ny * width_ + nx] = component_id;
                    queue.push({nx, ny});
                }
            }
        }
        
        bounds.x = min_x;
        bounds.y = min_y;
        bounds.w = max_x - min_x + 1;
        bounds.h = max_y - min_y + 1;
        
        return size;
    }
    
    bool is_near_sprite_pixel(int x, int y) const {
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
        std::vector<SpriteFrame> frames;
        frames.reserve(component_bounds_.size());
        
        // Sort components by area (largest first) to prioritize bigger sprites
        std::vector<std::pair<int, int>> component_areas_with_index;
        for (size_t i = 0; i < component_bounds_.size(); ++i) {
            component_areas_with_index.emplace_back(component_bounds_[i].area(), static_cast<int>(i));
        }
        std::sort(component_areas_with_index.rbegin(), component_areas_with_index.rend());
        
        for (size_t i = 0; i < component_areas_with_index.size() && i < static_cast<size_t>(config_.max_sprites); ++i) {
            int component_idx = component_areas_with_index[i].second;
            const Rectangle& bounds = component_bounds_[component_idx];
            
            SpriteFrame frame;
            frame.bounds = bounds;
            frame.index = static_cast<int>(i);
            frames.push_back(frame);
        }
        
        return frames;
    }
    
    bool output_frames(const std::vector<SpriteFrame>& frames) {
        // Default to spratframes format (minimalist)
        return output_spratframes(frames);
    }
    
    bool output_spratframes(const std::vector<SpriteFrame>& frames) {
        // First line: path <filepath>
        std::cout << "path " << config_.input_path.string() << "\n";
        
        // Check if we need to output background color
        if (config_.has_rectangles) {
            std::cout << "background " 
                      << static_cast<int>(config_.rectangle_color.r) << ","
                      << static_cast<int>(config_.rectangle_color.g) << ","
                      << static_cast<int>(config_.rectangle_color.b) << "\n";
        }
        
        // Each frame: sprite x,y w,h
        for (const auto& frame : frames) {
            std::cout << "sprite " << frame.bounds.x << "," << frame.bounds.y 
                      << " " << frame.bounds.w << "," << frame.bounds.h << "\n";
        }
        
        return true;
    }
    
    bool preprocess_image() {
        if (width_ <= 0 || height_ <= 0) {
            return false;
        }
        
        // Check the first pixel (top-left corner)
        Color first_pixel{image_data_[0], image_data_[1], image_data_[2], image_data_[3]};
        
        // If the first pixel is not transparent, make all pixels of that color transparent
        if (!first_pixel.is_transparent()) {
            // Make all pixels matching the first pixel color (within tolerance) transparent
            for (int i = 0; i < width_ * height_; ++i) {
                size_t idx = i * 4;
                Color pixel{image_data_[idx], image_data_[idx+1], image_data_[idx+2], image_data_[idx+3]};
                if (color_distance(pixel, first_pixel) <= 15) {
                    // Update the image data array as well
                    image_data_[i*4 + 3] = 0;
                }
            }
        }
        
        return true;
    }
};

// Utility functions
std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

bool parse_color(const std::string& value, Color& out) {
    if (value.empty()) return false;
    
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Handle hex colors
    if (lower.front() == '#') {
        std::string hex = lower.substr(1);
        if (hex.length() == 3) {
            // #RGB format
            int r = std::stoi(hex.substr(0, 1), nullptr, 16);
            int g = std::stoi(hex.substr(1, 1), nullptr, 16);
            int b = std::stoi(hex.substr(2, 1), nullptr, 16);
            out.r = static_cast<unsigned char>((r << 4) | r);
            out.g = static_cast<unsigned char>((g << 4) | g);
            out.b = static_cast<unsigned char>((b << 4) | b);
            out.a = 255;
            return true;
        } else if (hex.length() == 6) {
            // #RRGGBB format
            int r = std::stoi(hex.substr(0, 2), nullptr, 16);
            int g = std::stoi(hex.substr(2, 2), nullptr, 16);
            int b = std::stoi(hex.substr(4, 2), nullptr, 16);
            out.r = static_cast<unsigned char>(r);
            out.g = static_cast<unsigned char>(g);
            out.b = static_cast<unsigned char>(b);
            out.a = 255;
            return true;
        }
        return false;
    }
    
    // Handle RGB(r,g,b) format
    if (lower.substr(0, 4) == "rgb(" && lower.back() == ')') {
        std::string content = trim_copy(lower.substr(4, lower.length() - 5));
        std::istringstream iss(content);
        std::string r_str, g_str, b_str;
        
        if (std::getline(iss, r_str, ',') && 
            std::getline(iss, g_str, ',') && 
            std::getline(iss, b_str)) {
            
            try {
                int r = std::stoi(trim_copy(r_str));
                int g = std::stoi(trim_copy(g_str));
                int b = std::stoi(trim_copy(b_str));
                
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    out.r = static_cast<unsigned char>(r);
                    out.g = static_cast<unsigned char>(g);
                    out.b = static_cast<unsigned char>(b);
                    out.a = 255;
                    return true;
                }
            } catch (const std::exception&) {
                return false;
            }
        }
        return false;
    }
    
    // Handle comma-separated values
    std::istringstream iss(value);
    std::string r_str, g_str, b_str;
    if (std::getline(iss, r_str, ',') && 
        std::getline(iss, g_str, ',') && 
        std::getline(iss, b_str)) {
        
        try {
            int r = std::stoi(trim_copy(r_str));
            int g = std::stoi(trim_copy(g_str));
            int b = std::stoi(trim_copy(b_str));
            
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                out.r = static_cast<unsigned char>(r);
                out.g = static_cast<unsigned char>(g);
                out.b = static_cast<unsigned char>(b);
                out.a = 255;
                return true;
            }
        } catch (const std::exception&) {
            return false;
        }
    }
    
    return false;
}

bool parse_positive_int(const std::string& value, int& out) {
    try {
        size_t idx = 0;
        long long parsed = std::stoll(value, &idx);
        if (idx != value.size() || parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void print_usage() {
    std::cout << "Usage: spratframes [OPTIONS] <input_image>\n\n"
              << "Detect sprite frame rectangles in spritesheets.\n\n"
              << "Output format:\n"
              << "  SpratFrames format: path f, then sprite x,y w,h\n\n"
              << "Options:\n"
              << "  --has-rectangles          Spritesheet has rectangles surrounding sprites\n"
              << "  --rectangle-color COLOR   Color of rectangle borders (default: 255,0,255)\n"
              << "                            Formats: #RRGGBB, #RGB, RGB(r,g,b), r,g,b\n"
              << "  --tolerance N            Distance tolerance for sprite grouping (default: 1)\n"
              << "  --min-size N             Minimum sprite size in pixels (default: 4)\n"
              << "  --max-sprites N          Maximum number of sprites to extract (default: 10000)\n"
              << "  --threads N              Number of threads to use (default: 0 = auto)\n"
              << "  --help, -h               Show this help message\n\n"
              << "Examples:\n"
              << "  spratframes sheet.png\n"
              << "  spratframes --has-rectangles --rectangle-color=\"#FF00FF\" sheet.png\n"
              << "  spratframes --tolerance 2 --min-size 8 sheet.png\n"
              << "  spratframes sheet.png > frames.spratframes\n";
}

int main(int argc, char** argv) {
    FramesConfig config;
    bool show_help = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            show_help = true;
        } else if (arg == "--has-rectangles") {
            config.has_rectangles = true;
        } else if (arg == "--rectangle-color" && i + 1 < argc) {
            if (!parse_color(argv[++i], config.rectangle_color)) {
                std::cerr << "Error: Invalid color format: " << argv[i] << std::endl;
                return 1;
            }
        } else if (arg == "--tolerance" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.tolerance)) {
                std::cerr << "Error: Invalid tolerance value: " << argv[i] << std::endl;
                return 1;
            }
        } else if (arg == "--min-size" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.min_sprite_size)) {
                std::cerr << "Error: Invalid min-size value: " << argv[i] << std::endl;
                return 1;
            }
        } else if (arg == "--max-sprites" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], config.max_sprites)) {
                std::cerr << "Error: Invalid max-sprites value: " << argv[i] << std::endl;
                return 1;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            int threads_int = 0;
            if (!parse_positive_int(argv[++i], threads_int)) {
                std::cerr << "Error: Invalid threads value: " << argv[i] << std::endl;
                return 1;
            }
            config.threads = static_cast<unsigned int>(threads_int);
        } else if (arg.empty() || arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            print_usage();
            return 1;
        } else {
            // Positional arguments
            if (config.input_path.empty()) {
                config.input_path = argv[i];
            } else {
                std::cerr << "Error: Too many arguments" << std::endl;
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
        std::cerr << "Error: Input image path is required" << std::endl;
        print_usage();
        return 1;
    }
    
    // Validate input file exists
    if (!fs::exists(config.input_path) || !fs::is_regular_file(config.input_path)) {
        std::cerr << "Error: Input file does not exist or is not a file: " << config.input_path << std::endl;
        return 1;
    }
    
    // Set default threads if not specified
    if (config.threads == 0) {
        config.threads = std::max(1u, std::thread::hardware_concurrency());
    }
    
    // Detect frames
    SpriteFramesDetector detector(config);
    if (!detector.detect_frames()) {
        std::cerr << "Error: Failed to detect frames" << std::endl;
        return 1;
    }
    
    return 0;
}
