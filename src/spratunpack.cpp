// spratunpack.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 src/spratunpack.cpp -o spratunpack

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

// Libarchive for proper tar format
#include <archive.h>
#include <archive_entry.h>

namespace fs = std::filesystem;

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

// Configuration
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
    std::string filename;
    int index;
};

struct UnpackConfig {
    fs::path input_path;
    fs::path frames_path;
    fs::path output_dir;
    int min_sprite_size = k_default_min_sprite_size;
    int max_sprites = k_default_max_sprites;
    unsigned int threads = k_default_threads;
    bool verbose = false;
    bool force = false;
    std::string filename_pattern = "sprite_{index:04d}.png";
    bool output_to_stdout = false; // New: output tar-like format to stdout
    bool use_stdin_for_frames = false; // New: read frames from stdin
    std::vector<std::pair<fs::path, fs::path>> image_frame_pairs; // Multiple image/frame pairs
};

class SpriteUnpacker {
private:
    UnpackConfig config_;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<unsigned char> image_data_;
    
    std::vector<Rectangle> detected_rectangles_;
    
public:
    SpriteUnpacker(const UnpackConfig& config) : config_(config) {}
    
    bool load_image() {
        int req_channels = 4; // Always load with alpha
        unsigned char* data = stbi_load(config_.input_path.string().c_str(), &width_, &height_, &channels_, req_channels);
        
        if (!data) {
            std::cerr << "Error: Failed to load image: " << config_.input_path << std::endl;
            return false;
        }
        
        image_data_.assign(data, data + static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
        stbi_image_free(data);
        
        if (config_.verbose) {
            std::cout << "Loaded image: " << config_.input_path << " (" << width_ << "x" << height_ << ", " << channels_ << " channels)" << std::endl;
        }
        
        return true;
    }
    
    bool load_frames() {
        if (config_.frames_path.empty()) {
            // No frames file specified, read from stdin
            return load_frames_from_stream(std::cin);
        } else {
            if (!fs::exists(config_.frames_path) || !fs::is_regular_file(config_.frames_path)) {
                std::cerr << "Error: Frames file does not exist or is not a file: " << config_.frames_path << std::endl;
                return false;
            }
            
            std::ifstream file(config_.frames_path);
            if (!file.is_open()) {
                std::cerr << "Error: Failed to open frames file: " << config_.frames_path << std::endl;
                return false;
            }
            
            // Determine file format by extension
            std::string extension = config_.frames_path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            
            if (extension == ".json") {
                return load_json_frames(file);
            } else if (extension == ".csv") {
                return load_csv_frames(file);
            } else {
                // Try to detect format by content
                return load_auto_frames(file);
            }
        }
    }
    
    bool load_frames_from_stream(std::istream& stream) {
        detected_rectangles_.clear();
        
        // Read all content from stream
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        
        // Create a string stream to work with
        std::istringstream iss(content);
        
        // Try to detect format by first line content
        std::string first_line;
        std::getline(iss, first_line);
        iss.clear();
        iss.seekg(0);
        
        if (first_line.find('{') != std::string::npos && first_line.find("frames") != std::string::npos) {
            return load_json_frames(iss);
        } else if (first_line.find(',') != std::string::npos) {
            return load_csv_frames(iss);
        } else if (first_line.find("path ") == 0 || first_line.find("spritesheets/") == 0) {
            return load_spratframes_frames(iss);
        } else {
            return load_plain_frames(iss);
        }
    }
    
    bool unpack_sprites() {
        if (config_.output_to_stdout) {
            return unpack_to_stdout();
        } else {
            return unpack_to_files();
        }
    }
    
    bool unpack_to_files() {
        if (!load_image()) {
            return false;
        }
        
        if (!load_frames()) {
            return false;
        }
        
        if (!fs::exists(config_.output_dir)) {
            if (!fs::create_directories(config_.output_dir)) {
                std::cerr << "Error: Failed to create output directory: " << config_.output_dir << std::endl;
                return false;
            }
        }
        
        std::vector<SpriteFrame> sprites;
        sprites.reserve(detected_rectangles_.size());
        
        for (size_t i = 0; i < detected_rectangles_.size() && i < static_cast<size_t>(config_.max_sprites); ++i) {
            Rectangle rect = detected_rectangles_[i];
            
            // Ensure bounds are valid
            if (rect.w <= 0 || rect.h <= 0) continue;
            if (rect.x < 0) { rect.w += rect.x; rect.x = 0; }
            if (rect.y < 0) { rect.h += rect.y; rect.y = 0; }
            if (rect.right() > width_) rect.w = width_ - rect.x;
            if (rect.bottom() > height_) rect.h = height_ - rect.y;
            
            if (rect.w > 0 && rect.h > 0) {
                SpriteFrame sprite;
                sprite.bounds = rect;
                sprite.index = static_cast<int>(i);
                sprite.filename = generate_filename(i);
                sprites.push_back(sprite);
            }
        }
        
        if (sprites.empty()) {
            std::cerr << "Warning: No sprites found" << std::endl;
            return true;
        }
        
        return save_sprites(sprites);
    }
    
    bool unpack_to_stdout() {
        // Load image
        if (!load_image()) {
            return false;
        }
        
        // Load frames from stdin (already done in load_frames())
        if (!load_frames()) {
            return false;
        }
        
        // Use libarchive to create tar in memory
        struct archive* a = archive_write_new();
        if (!a) {
            std::cerr << "Error: Failed to create archive" << std::endl;
            return false;
        }
        
        // Set format to tar
        if (archive_write_set_format_pax_restricted(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to set archive format: " << archive_error_string(a) << std::endl;
            archive_write_free(a);
            return false;
        }
        
        // Set compression to none (raw tar)
        if (archive_write_add_filter_none(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to set compression: " << archive_error_string(a) << std::endl;
            archive_write_free(a);
            return false;
        }
        
        // Set output to memory
        std::vector<char> archive_buffer;
        archive_buffer.reserve(1024 * 1024); // Start with 1MB buffer
        
        // Custom callback to write to our buffer
        auto write_callback = [](struct archive*, void* client_data, const void* buffer, size_t length) -> la_ssize_t {
            std::vector<char>* buf = static_cast<std::vector<char>*>(client_data);
            const char* data = static_cast<const char*>(buffer);
            buf->insert(buf->end(), data, data + length);
            return static_cast<la_ssize_t>(length);
        };
        
        if (archive_write_open(a, &archive_buffer, nullptr, write_callback, nullptr) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to open memory for archive: " << archive_error_string(a) << std::endl;
            archive_write_free(a);
            return false;
        }
        
        // Write all sprites to the archive
        for (size_t i = 0; i < detected_rectangles_.size() && i < static_cast<size_t>(config_.max_sprites); ++i) {
            Rectangle rect = detected_rectangles_[i];
            
            // Ensure bounds are valid
            if (rect.w <= 0 || rect.h <= 0) continue;
            if (rect.x < 0) { rect.w += rect.x; rect.x = 0; }
            if (rect.y < 0) { rect.h += rect.y; rect.y = 0; }
            if (rect.right() > width_) rect.w = width_ - rect.x;
            if (rect.bottom() > height_) rect.h = height_ - rect.y;
            
            if (rect.w > 0 && rect.h > 0) {
                std::string filename = generate_filename(static_cast<int>(i));
                if (!write_sprite_to_archive_entry(a, rect, filename)) {
                    archive_write_free(a);
                    return false;
                }
            }
        }
        
        // Close the archive
        if (archive_write_close(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to close archive: " << archive_error_string(a) << std::endl;
            archive_write_free(a);
            return false;
        }
        
        // Get the actual size of the archive
        size_t archive_size = archive_buffer.size();
        
        // Output the archive to stdout
        std::cout.write(archive_buffer.data(), archive_size);
        
        archive_write_free(a);
        
        return true;
    }
    
    bool load_image_for_path(const fs::path& img_path) {
        int req_channels = 4; // Always load with alpha
        unsigned char* data = stbi_load(img_path.string().c_str(), &width_, &height_, &channels_, req_channels);
        
        if (!data) {
            std::cerr << "Error: Failed to load image: " << img_path << std::endl;
            return false;
        }
        
        image_data_.assign(data, data + static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
        stbi_image_free(data);
        
        return true;
    }
    
    bool load_frames_for_path(const fs::path& frames_path) {
        if (frames_path.empty()) {
            // Empty path indicates we should read from stdin
            return load_frames_from_stream(std::cin);
        }
        
        if (!fs::exists(frames_path) || !fs::is_regular_file(frames_path)) {
            std::cerr << "Error: Frames file does not exist or is not a file: " << frames_path << std::endl;
            return false;
        }
        
        std::ifstream file(frames_path);
        if (!file.is_open()) {
            std::cerr << "Error: Failed to open frames file: " << frames_path << std::endl;
            return false;
        }
        
        // Determine file format by extension
        std::string extension = frames_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        if (extension == ".json") {
            return load_json_frames(file);
        } else if (extension == ".csv") {
            return load_csv_frames(file);
        } else {
            // Try to detect format by content
            return load_auto_frames(file);
        }
    }
    
    bool write_sprite_to_archive_entry(struct archive* a, const Rectangle& rect, const std::string& filename) {
        const Rectangle& bounds = rect;
        std::vector<unsigned char> sprite_data(bounds.w * bounds.h * 4);
        
        for (int y = 0; y < bounds.h; ++y) {
            for (int x = 0; x < bounds.w; ++x) {
                int src_idx = ((bounds.y + y) * width_ + (bounds.x + x)) * 4;
                int dst_idx = (y * bounds.w + x) * 4;
                
                sprite_data[dst_idx] = image_data_[src_idx];     // R
                sprite_data[dst_idx + 1] = image_data_[src_idx + 1]; // G
                sprite_data[dst_idx + 2] = image_data_[src_idx + 2]; // B
                sprite_data[dst_idx + 3] = image_data_[src_idx + 3]; // A
            }
        }
        
        // Encode PNG data
        int png_size = 0;
        unsigned char* png_buffer = stbi_write_png_to_mem(sprite_data.data(), bounds.w * 4, 
                                                        bounds.w, bounds.h, 4, &png_size);
        if (!png_buffer || png_size <= 0) {
            std::cerr << "Error: Failed to encode PNG data" << std::endl;
            return false;
        }
        
        // Create archive entry
        struct archive_entry* entry = archive_entry_new();
        if (!entry) {
            std::cerr << "Error: Failed to create archive entry" << std::endl;
            free(png_buffer);
            return false;
        }
        
        archive_entry_set_pathname(entry, filename.c_str());
        archive_entry_set_size(entry, png_size);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_mtime(entry, time(nullptr), 0);
        
        // Write header
        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to write archive header: " << archive_error_string(a) << std::endl;
            archive_entry_free(entry);
            free(png_buffer);
            return false;
        }
        
        // Write data
        if (archive_write_data(a, png_buffer, png_size) != static_cast<ssize_t>(png_size)) {
            std::cerr << "Error: Failed to write archive data: " << archive_error_string(a) << std::endl;
            archive_entry_free(entry);
            free(png_buffer);
            return false;
        }
        
        // Free resources
        archive_entry_free(entry);
        free(png_buffer);
        
        return true;
    }
    
private:
    bool load_json_frames(std::istream& stream) {
        detected_rectangles_.clear();
        
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        
        // Simple JSON parsing (not using external library for minimal dependencies)
        // Look for "frames" key
        size_t key_pos = content.find("\"frames\"");
        if (key_pos == std::string::npos) {
            std::cerr << "Error: Invalid JSON format - missing 'frames' array" << std::endl;
            return false;
        }
        
        // Find start of array
        size_t frames_start = content.find("[", key_pos);
        if (frames_start == std::string::npos) {
            std::cerr << "Error: Invalid JSON format - missing '[' after 'frames'" << std::endl;
            return false;
        }
        
        size_t frames_end = content.find("]", frames_start);
        if (frames_end == std::string::npos) {
            std::cerr << "Error: Invalid JSON format - unclosed 'frames' array" << std::endl;
            return false;
        }
        
        std::string frames_content = content.substr(frames_start + 1, frames_end - frames_start - 1);
        
        // Parse individual frame objects
        size_t pos = 0;
        while (pos < frames_content.length()) {
            // Find next object
            size_t obj_start = frames_content.find("{", pos);
            if (obj_start == std::string::npos) break;
            
            size_t obj_end = frames_content.find("}", obj_start);
            if (obj_end == std::string::npos) break;
            
            std::string obj_content = frames_content.substr(obj_start + 1, obj_end - obj_start - 1);
            
            Rectangle rect;
            if (parse_frame_object(obj_content, rect)) {
                detected_rectangles_.push_back(rect);
            }
            
            pos = obj_end + 1;
        }
        
        if (config_.verbose) {
            std::cout << "Loaded " << detected_rectangles_.size() << " frames from JSON" << std::endl;
        }
        
        return !detected_rectangles_.empty();
    }
    
    bool parse_frame_object(const std::string& obj_content, Rectangle& rect) {
        // Simple key-value parsing
        std::istringstream iss(obj_content);
        std::string line;
        
        while (std::getline(iss, line, ',')) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            
            std::string key = trim_copy(line.substr(0, colon));
            std::string value = trim_copy(line.substr(colon + 1));
            
            // Remove quotes from key
            if (key.front() == '"' && key.back() == '"') {
                key = key.substr(1, key.length() - 2);
            }
            
            // Remove quotes from value
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            if (key == "\"x\"" || key == "x") {
                rect.x = std::stoi(value);
            } else if (key == "\"y\"" || key == "y") {
                rect.y = std::stoi(value);
            } else if (key == "\"w\"" || key == "w") {
                rect.w = std::stoi(value);
            } else if (key == "\"h\"" || key == "h") {
                rect.h = std::stoi(value);
            }
        }
        
        return rect.w > 0 && rect.h > 0;
    }
    
    bool load_csv_frames(std::istream& stream) {
        detected_rectangles_.clear();
        
        std::string line;
        bool header_read = false;
        
        while (std::getline(stream, line)) {
            if (!header_read) {
                header_read = true;
                continue; // Skip header
            }
            
            std::istringstream iss(line);
            std::string x_str, y_str, w_str, h_str;
            
            if (std::getline(iss, x_str, ',') && 
                std::getline(iss, y_str, ',') && 
                std::getline(iss, w_str, ',') && 
                std::getline(iss, h_str)) {
                
                try {
                    Rectangle rect;
                    rect.x = std::stoi(trim_copy(x_str));
                    rect.y = std::stoi(trim_copy(y_str));
                    rect.w = std::stoi(trim_copy(w_str));
                    rect.h = std::stoi(trim_copy(h_str));
                    
                    if (rect.w > 0 && rect.h > 0) {
                        detected_rectangles_.push_back(rect);
                    }
                } catch (const std::exception&) {
                    // Skip invalid lines
                }
            }
        }
        
        if (config_.verbose) {
            std::cout << "Loaded " << detected_rectangles_.size() << " frames from CSV" << std::endl;
        }
        
        return !detected_rectangles_.empty();
    }
    
    bool load_spratframes_frames(std::istream& stream) {
        detected_rectangles_.clear();
        
        std::string line;
        bool path_read = false;
        
        while (std::getline(stream, line)) {
            std::istringstream iss(line);
            std::string token;
            
            if (!path_read) {
                // First line should be "path <filepath>" or just "<filepath> f"
                if (std::getline(iss, token, ' ')) {
                    if (token == "path") {
                        // Skip the path value
                        std::string path_value;
                        std::getline(iss, path_value);
                    } else {
                        // Handle format without "path" prefix: "<filepath> f"
                        // Just skip this line
                    }
                    path_read = true;
                }
                continue;
            }
            
            // Skip background color line if present
            if (line.find("background ") == 0) {
                continue;
            }
            
            // Parse sprite lines: "sprite x,y w,h"
            if (std::getline(iss, token, ' ') && token == "sprite") {
                std::string coords_str, size_str;
                if (std::getline(iss, coords_str, ' ') && std::getline(iss, size_str)) {
                    // Parse coordinates: "x,y"
                    std::istringstream coords_iss(coords_str);
                    std::string x_str, y_str;
                    if (std::getline(coords_iss, x_str, ',') && std::getline(coords_iss, y_str)) {
                        // Parse size: "w,h"
                        std::istringstream size_iss(size_str);
                        std::string w_str, h_str;
                        if (std::getline(size_iss, w_str, ',') && std::getline(size_iss, h_str)) {
                            try {
                                Rectangle rect;
                                rect.x = std::stoi(trim_copy(x_str));
                                rect.y = std::stoi(trim_copy(y_str));
                                rect.w = std::stoi(trim_copy(w_str));
                                rect.h = std::stoi(trim_copy(h_str));
                                
                                if (rect.w > 0 && rect.h > 0) {
                                    detected_rectangles_.push_back(rect);
                                }
                            } catch (const std::exception&) {
                                // Skip invalid lines
                            }
                        }
                    }
                }
            }
        }
        
        if (config_.verbose) {
            std::cout << "Loaded " << detected_rectangles_.size() << " frames from SpratFrames format" << std::endl;
        }
        
        return !detected_rectangles_.empty();
    }
    
    bool load_auto_frames(std::istream& stream) {
        // Try to detect format by first line content
        std::string first_line;
        std::getline(stream, first_line);
        stream.clear();
        stream.seekg(0);
        
        if (first_line.find('{') != std::string::npos && first_line.find("frames") != std::string::npos) {
            return load_json_frames(stream);
        } else if (first_line.find(',') != std::string::npos) {
            return load_csv_frames(stream);
        } else if (first_line.find("path ") == 0 || first_line.find("spritesheets/") == 0) {
            return load_spratframes_frames(stream);
        } else {
            return load_plain_frames(stream);
        }
    }
    
    bool load_plain_frames(std::istream& stream) {
        detected_rectangles_.clear();
        
        std::string line;
        while (std::getline(stream, line)) {
            std::istringstream iss(line);
            std::string x_str, y_str, w_str, h_str;
            
            if (std::getline(iss, x_str, ' ') && 
                std::getline(iss, y_str, ' ') && 
                std::getline(iss, w_str, ' ') && 
                std::getline(iss, h_str)) {
                
                try {
                    Rectangle rect;
                    rect.x = std::stoi(trim_copy(x_str));
                    rect.y = std::stoi(trim_copy(y_str));
                    rect.w = std::stoi(trim_copy(w_str));
                    rect.h = std::stoi(trim_copy(h_str));
                    
                    if (rect.w > 0 && rect.h > 0) {
                        detected_rectangles_.push_back(rect);
                    }
                } catch (const std::exception&) {
                    // Skip invalid lines
                }
            }
        }
        
        if (config_.verbose) {
            std::cout << "Loaded " << detected_rectangles_.size() << " frames from plain text" << std::endl;
        }
        
        return !detected_rectangles_.empty();
    }
    
    std::string generate_filename(int index) const {
        std::string filename = config_.filename_pattern;
        
        // Replace {index} placeholder
        size_t pos = 0;
        while ((pos = filename.find("{index", pos)) != std::string::npos) {
            size_t end = filename.find("}", pos);
            if (end != std::string::npos) {
                std::string format_spec = filename.substr(pos + 1, end - pos - 1);
                std::string replacement = format_index(index, format_spec);
                filename.replace(pos, end - pos + 1, replacement);
                pos += replacement.length();
            } else {
                pos++;
            }
        }
        
        return filename;
    }
    
    std::string format_index(int index, const std::string& format_spec) const {
        // Simple format parser: {index:04d} -> 4 digits with leading zeros
        size_t colon = format_spec.find(':');
        if (colon == std::string::npos) {
            return std::to_string(index);
        }
        
        std::string format_str = format_spec.substr(colon + 1);
        if (format_str == "04d") {
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(4) << index;
            return oss.str();
        } else if (format_str == "03d") {
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(3) << index;
            return oss.str();
        } else if (format_str == "02d") {
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(2) << index;
            return oss.str();
        }
        
        return std::to_string(index);
    }
    
    bool save_sprites(const std::vector<SpriteFrame>& sprites) {
        if (config_.verbose) {
            std::cout << "Saving " << sprites.size() << " sprites to " << config_.output_dir << std::endl;
        }
        
        for (const auto& sprite : sprites) {
            fs::path output_path = config_.output_dir / sprite.filename;
            
            if (!config_.force && fs::exists(output_path)) {
                std::cerr << "Warning: File already exists, skipping: " << output_path << std::endl;
                continue;
            }
            
            if (!save_sprite_image(sprite)) {
                std::cerr << "Error: Failed to save sprite: " << output_path << std::endl;
                return false;
            }
            
            if (config_.verbose) {
                std::cout << "Saved: " << output_path << " (" << sprite.bounds.w << "x" << sprite.bounds.h << ")" << std::endl;
            }
        }
        
        return true;
    }
    
    bool save_sprite_image(const SpriteFrame& sprite) {
        const Rectangle& bounds = sprite.bounds;
        std::vector<unsigned char> sprite_data(bounds.w * bounds.h * 4);
        
        for (int y = 0; y < bounds.h; ++y) {
            for (int x = 0; x < bounds.w; ++x) {
                int src_idx = ((bounds.y + y) * width_ + (bounds.x + x)) * 4;
                int dst_idx = (y * bounds.w + x) * 4;
                
                sprite_data[dst_idx] = image_data_[src_idx];     // R
                sprite_data[dst_idx + 1] = image_data_[src_idx + 1]; // G
                sprite_data[dst_idx + 2] = image_data_[src_idx + 2]; // B
                sprite_data[dst_idx + 3] = image_data_[src_idx + 3]; // A
            }
        }
        
        fs::path output_path = config_.output_dir / sprite.filename;
        return stbi_write_png(output_path.string().c_str(), 
                             bounds.w, bounds.h, 4, sprite_data.data(), 
                             bounds.w * 4) != 0;
    }
};

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
    std::cout << "Usage: spratunpack [OPTIONS] <input_image> <frames_file> <output_directory>\n\n"
              << "Extract sprite frames from spritesheets using frame coordinates.\n\n"
              << "Input formats:\n"
              << "  JSON: {\"width\": W, \"height\": H, \"frames\": [{\"x\": X, \"y\": Y, \"w\": W, \"h\": H}, ...]}\n"
              << "  CSV:  index,x,y,w,h (with header)\n"
              << "  Plain: x y w h (one per line)\n\n"
              << "Options:\n"
              << "  --min-size N             Minimum sprite size in pixels (default: 4)\n"
              << "  --max-sprites N          Maximum number of sprites to extract (default: 10000)\n"
              << "  --threads N              Number of threads to use (default: 0 = auto)\n"
              << "  --filename-pattern PATTERN  Output filename pattern (default: \"sprite_{index:04d}.png\")\n"
              << "  --verbose, -v            Enable verbose output\n"
              << "  --force, -f              Overwrite existing files\n"
              << "  --help, -h               Show this help message\n\n"
              << "Examples:\n"
              << "  spratunpack sheet.png frames.json output/\n"
              << "  spratunpack sheet.png frames.csv output/\n"
              << "  spratunpack sheet.png frames.txt output/\n"
              << "  spratunpack --filename-pattern \"frame_{index:03d}.png\" sheet.png frames.json output/\n";
}

int main(int argc, char** argv) {
    UnpackConfig config;
    bool show_help = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            show_help = true;
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
        } else if (arg == "--filename-pattern" && i + 1 < argc) {
            config.filename_pattern = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else if (arg == "--force" || arg == "-f") {
            config.force = true;
        } else if (arg.empty() || arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            print_usage();
            return 1;
        } else {
            // Positional arguments
            if (config.input_path.empty()) {
                config.input_path = argv[i];
            } else if (config.frames_path.empty()) {
                config.frames_path = argv[i];
            } else if (config.output_dir.empty()) {
                config.output_dir = argv[i];
            } else {
                // Additional image/frame pairs for multiple image support
                if (config.image_frame_pairs.empty() || config.image_frame_pairs.back().second != fs::path()) {
                    config.image_frame_pairs.push_back({argv[i], fs::path()});
                } else {
                    config.image_frame_pairs.back().second = argv[i];
                }
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
    
    // Frames file is optional - if not provided, will read from stdin
    
    // If no output directory specified, use stdout mode
    if (config.output_dir.empty()) {
        config.output_to_stdout = true;
    }

#if defined(_WIN32)
    if (config.output_to_stdout) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif
    
    // Validate input file exists
    if (!fs::exists(config.input_path) || !fs::is_regular_file(config.input_path)) {
        std::cerr << "Error: Input file does not exist or is not a file: " << config.input_path << std::endl;
        return 1;
    }
    
    // Set default threads if not specified
    if (config.threads == 0) {
        config.threads = std::max(1u, std::thread::hardware_concurrency());
    }
    
    if (config.verbose) {
        std::cout << "Configuration:\n";
        std::cout << "  Input: " << config.input_path << "\n";
        std::cout << "  Frames: " << config.frames_path << "\n";
        std::cout << "  Output: " << config.output_dir << "\n";
        std::cout << "  Min size: " << config.min_sprite_size << "\n";
        std::cout << "  Max sprites: " << config.max_sprites << "\n";
        std::cout << "  Threads: " << config.threads << "\n";
        std::cout << "  Filename pattern: " << config.filename_pattern << "\n";
        std::cout << "  Verbose: " << (config.verbose ? "yes" : "no") << "\n";
        std::cout << "  Force: " << (config.force ? "yes" : "no") << "\n\n";
    }
    
    // Unpack sprites
    SpriteUnpacker unpacker(config);
    if (!unpacker.unpack_sprites()) {
        std::cerr << "Error: Failed to unpack sprites" << std::endl;
        return 1;
    }
    
    if (config.verbose) {
        std::cout << "Sprite unpacking completed successfully!" << std::endl;
    }
    
    return 0;
}