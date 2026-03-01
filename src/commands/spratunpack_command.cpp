// spratunpack.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 src/spratunpack.cpp -o spratunpack

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
namespace fs = std::filesystem;
#include <cctype>
#include <cstring>
#include <limits>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <iterator>
#include "core/cli_parse.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif
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

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

// Libarchive for proper tar format
#include <archive.h>
#include <archive_entry.h>

namespace {
using sprat::core::parse_non_negative_uint;
using sprat::core::to_quoted;

constexpr int NUM_CHANNELS = 4;

struct Rectangle {
    int x, y, w, h;
};

struct SpriteFrame {
    std::string name;
    Rectangle frame{};
    Rectangle sprite_source_size{};
    int source_w{}, source_h{};
    bool rotated{};
    bool trimmed{};
};

struct Config {
    fs::path input_path;
    fs::path detected_input_path;
    fs::path frames_path;
    fs::path output_dir;
    bool input_from_stdin = false;
    bool frames_from_stdin = false;
    bool stdout_mode = false;
    unsigned int threads = 0;
};

class SpriteUnpacker {
public:
    SpriteUnpacker(Config  config) : config_(std::move(config)) {}

    bool run() {
        if (!load_frames()) { return false;
}
        if (!load_image()) { return false;
}

        if (config_.stdout_mode) {
            return unpack_to_stdout();
        }             return unpack_to_dir();
       
    }

private:
    Config config_;
    int width_{}, height_{}, channels_{};
    std::vector<unsigned char> image_data_;
    std::vector<SpriteFrame> frames_;

    bool load_image() {
        unsigned char* data = nullptr;
        fs::path actual_input_path = config_.input_path;
        if (actual_input_path.empty() && !config_.detected_input_path.empty()) {
            actual_input_path = config_.detected_input_path;
        }

        if (actual_input_path.empty() && config_.input_from_stdin) {
            const std::vector<unsigned char> stdin_bytes(
                std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
            if (stdin_bytes.empty()) {
                std::cerr << "Error: No atlas image data received on stdin\n";
                return false;
            }
            data = stbi_load_from_memory(
                stdin_bytes.data(),
                static_cast<int>(stdin_bytes.size()),
                &width_,
                &height_,
                &channels_,
                NUM_CHANNELS);
        } else if (!actual_input_path.empty()) {
            data = stbi_load(
                actual_input_path.string().c_str(), &width_, &height_, &channels_, NUM_CHANNELS);
        } else {
            std::cerr << "Error: No input atlas provided.\n";
            return false;
        }

        if (data == nullptr) {
            if (actual_input_path.empty()) {
                std::cerr << "Error: Failed to load atlas image from stdin\n";
            } else {
                std::cerr << "Error: Failed to load image " << to_quoted(actual_input_path.string()) << "\n";
            }
            return false;
        }

        image_data_.assign(data, data + (static_cast<size_t>(width_) * height_ * NUM_CHANNELS));
        stbi_image_free(data);
        return true;
    }

    bool load_frames() {
        std::string content;
        std::string extension;

        if (config_.frames_from_stdin || (config_.frames_path.empty() && config_.input_from_stdin)) {
            content.assign(std::istreambuf_iterator<char>(std::cin),
                          std::istreambuf_iterator<char>());
            if (content.empty()) {
                if (config_.frames_from_stdin) {
                    std::cerr << "Error: No frames data received on stdin\n";
                    return false;
                }
                // If we were just hoping for frames on stdin because input was missing,
                // but got nothing, we'll fail later in load_image.
                // But wait, if we are here, input_from_stdin is true.
                std::cerr << "Error: No data received on stdin. Expected atlas image or frames definition.\n";
                return false;
            }
            // For stdin, we try to detect format from content
            if (content.find("\"frames\":") != std::string::npos || content.find('[') != std::string::npos) {
                extension = ".json";
            } else {
                extension = ".spratframes";
            }
            // If we read from stdin for frames, we must NOT read from it for image
            if (config_.input_from_stdin) {
                config_.input_from_stdin = false;
            }
        } else {
            if (config_.frames_path.empty()) {
                if (config_.input_path.empty()) {
                    // Magic mode: spratframes atlas.png | spratunpack
                    content.assign(std::istreambuf_iterator<char>(std::cin),
                                  std::istreambuf_iterator<char>());
                    if (content.empty()) {
                        std::cerr << "Error: No data received on stdin. Expected frames definition.\n";
                        return false;
                    }
                    if (content.find("\"frames\":") != std::string::npos || content.find('[') != std::string::npos) {
                        extension = ".json";
                    } else {
                        extension = ".spratframes";
                    }
                } else {
                    // Try to auto-detect frames file
                    fs::path json_path = config_.input_path;
                    json_path.replace_extension(".json");
                    if (fs::exists(json_path)) {
                        config_.frames_path = json_path;
                    } else {
                        fs::path sprat_path = config_.input_path;
                        sprat_path.replace_extension(".spratframes");
                        if (fs::exists(sprat_path)) {
                            config_.frames_path = sprat_path;
                        } else {
                            // Try stdin as fallback if auto-detect fails
                            content.assign(std::istreambuf_iterator<char>(std::cin),
                                          std::istreambuf_iterator<char>());
                            if (!content.empty()) {
                                if (content.find("\"frames\":") != std::string::npos || content.find('[') != std::string::npos) {
                                    extension = ".json";
                                } else {
                                    extension = ".spratframes";
                                }
                            } else {
                                std::cerr << "Error: Frames file not found and could not be auto-detected.\n";
                                return false;
                            }
                        }
                    }
                }
            }

            if (content.empty()) {
                extension = config_.frames_path.extension().string();
                std::ranges::transform(extension, extension.begin(), ::tolower);

                std::ifstream file(config_.frames_path);
                if (!file.is_open()) {
                    std::cerr << "Error: Failed to open frames file " << to_quoted(config_.frames_path.string()) << "\n";
                    return false;
                }

                std::stringstream ss;
                ss << file.rdbuf();
                content = ss.str();
            }
        }

        if (extension == ".json") {
            return parse_json(content);
        }
        if (extension == ".spratframes" || extension == ".txt") {
            return parse_spratframes(content);
        }

        // Try to auto-detect from content if extension is unknown
        if (content.find("\"frames\":") != std::string::npos || content.find("[") != std::string::npos) {
            if (parse_json(content)) {
                return true;
            }
        }

        if (parse_spratframes(content)) {
            return true;
        }

        std::cerr << "Error: Unsupported frames format " << extension << " and could not auto-detect format from content.\n";
        return false;
    }

    bool parse_spratframes(const std::string& content) {
        std::istringstream iss(content);
        std::string line;
        int index = 0;
        while (std::getline(iss, line)) {
            line = trim_copy(line);
            if (line.empty()) {
                continue;
            }

            if (line.starts_with("path ")) {
                if (config_.detected_input_path.empty()) {
                    std::string path_line = line.substr(5);
                    size_t pos = 0;
                    while (pos < path_line.size() && std::isspace(static_cast<unsigned char>(path_line[pos]))) {
                        pos++;
                    }
                    if (pos < path_line.size() && path_line[pos] == '"') {
                        std::string path;
                        std::string error;
                        if (sprat::core::parse_quoted(path_line, pos, path, error)) {
                            config_.detected_input_path = path;
                        }
                    } else {
                        config_.detected_input_path = trim_copy(path_line);
                    }
                }
                continue;
            }
            
            if (line.starts_with("background ")) {
                continue;
            }

            if (line.starts_with("sprite ")) {
                std::string sprite_line = line.substr(7);
                size_t pos = 0;
                while (pos < sprite_line.size() && std::isspace(static_cast<unsigned char>(sprite_line[pos]))) {
                    pos++;
                }
                
                std::string name;
                if (pos < sprite_line.size() && sprite_line[pos] == '"') {
                    std::string error;
                    if (!sprat::core::parse_quoted(sprite_line, pos, name, error)) {
                        continue;
                    }
                }

                std::vector<std::string> tokens;
                std::istringstream liss(sprite_line.substr(pos));
                std::string token;
                while (liss >> token) {
                    tokens.push_back(token);
                }

                if (tokens.size() >= 2) {
                    SpriteFrame frame;
                    frame.name = name.empty() ? "sprite_" + std::to_string(index++) : name;
                    
                    if (!sprat::core::parse_pair(tokens[0], frame.frame.x, frame.frame.y) ||
                        !sprat::core::parse_pair(tokens[1], frame.frame.w, frame.frame.h)) {
                        continue;
                    }

                    for (const auto& token : tokens) {
                        if (token == "rotated") {
                            frame.rotated = true;
                            break;
                        }
                    }
                    
                    frames_.push_back(frame);
                }
            }
        }
        return !frames_.empty();
    }

    static std::string trim_copy(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && (std::isspace(static_cast<unsigned char>(s[start])) != 0)) {
            ++start;
        }
        size_t end = s.size();
        while (end > start && (std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)) {
            --end;
        }
        return s.substr(start, end - start);
    }

    bool parse_json(const std::string& content) {
        // Very basic JSON parser for TexturePacker/sprat format
        size_t frames_start = content.find("\"frames\":");
        if (frames_start == std::string::npos) {
            // Check if it's an array-based format
            if (content.find('[') != std::string::npos) {
                return parse_json_array(content);
            }
            std::cerr << "Error: Invalid JSON format (missing \"frames\")\n";
            return false;
        }

        // Detect if frames is an object or array
        constexpr size_t JSON_FRAMES_KEY_OFFSET = 9; // strlen("\"frames\":")
        size_t search_pos = frames_start + JSON_FRAMES_KEY_OFFSET;
        while (search_pos < content.size() && (std::isspace(content[search_pos]) != 0)) { search_pos++;
}
        
        if (search_pos < content.size() && content[search_pos] == '[') {
            return parse_json_array(content.substr(search_pos));
        }             return parse_json_object(content.substr(search_pos));
       
    }

    bool parse_json_object(const std::string& content) {
        size_t pos = 0;
        while (true) {
            size_t key_start = content.find('\"', pos);
            if (key_start == std::string::npos) { break;
}
            size_t key_end = content.find('\"', key_start + 1);
            if (key_end == std::string::npos) { break;
}

            std::string key = content.substr(key_start + 1, key_end - key_start - 1);
            
            size_t obj_start = content.find('{', key_end);
            if (obj_start == std::string::npos) { break;
}
            
            size_t obj_end = find_closing_bracket(content, obj_start, '{', '}');
            if (obj_end == std::string::npos) { break;
}

            std::string obj_content = content.substr(obj_start, obj_end - obj_start + 1);
            SpriteFrame frame;
            frame.name = key;
            if (parse_frame_details(obj_content, frame)) {
                frames_.push_back(frame);
            }

            pos = obj_end + 1;
        }
        return !frames_.empty();
    }

    bool parse_json_array(const std::string& content) {
        size_t pos = 0;
        while (true) {
            size_t obj_start = content.find('{', pos);
            if (obj_start == std::string::npos) { break;
}
            
            size_t obj_end = find_closing_bracket(content, obj_start, '{', '}');
            if (obj_end == std::string::npos) { break;
}

            std::string obj_content = content.substr(obj_start, obj_end - obj_start + 1);
            
            // Extract name
            size_t name_pos = obj_content.find("\"filename\":");
            if (name_pos != std::string::npos) {
                constexpr size_t JSON_FILENAME_KEY_OFFSET = 11; // strlen("\"filename\":")
                size_t v_start = obj_content.find('\"', name_pos + JSON_FILENAME_KEY_OFFSET);
                size_t v_end = obj_content.find('\"', v_start + 1);
                if (v_start != std::string::npos && v_end != std::string::npos) {
                    SpriteFrame frame;
                    frame.name = obj_content.substr(v_start + 1, v_end - v_start - 1);
                    if (parse_frame_details(obj_content, frame)) {
                        frames_.push_back(frame);
                    }
                }
            }

            pos = obj_end + 1;
        }
        return !frames_.empty();
    }

    bool parse_frame_details(const std::string& content, SpriteFrame& frame) {
        frame.frame = extract_rect(content, "\"frame\":");
        frame.sprite_source_size = extract_rect(content, "\"spriteSourceSize\":");
        
        size_t source_size_pos = content.find("\"sourceSize\":");
        if (source_size_pos != std::string::npos) {
            frame.source_w = extract_int(content, "\"w\":", source_size_pos);
            frame.source_h = extract_int(content, "\"h\":", source_size_pos);
        }

        frame.rotated = content.find("\"rotated\": true") != std::string::npos;
        frame.trimmed = content.find("\"trimmed\": true") != std::string::npos;
        
        return true;
    }

    Rectangle extract_rect(const std::string& content, const std::string& key) {
        size_t pos = content.find(key);
        if (pos == std::string::npos) {
            return {.x=0, .y=0, .w=0, .h=0};
        }
        
        return {
            .x=extract_int(content, "\"x\":", pos),
            .y=extract_int(content, "\"y\":", pos),
            .w=extract_int(content, "\"w\":", pos),
            .h=extract_int(content, "\"h\":", pos)
        };
    }

    static int extract_int(const std::string& content, const std::string& key, size_t start_pos) {
        size_t pos = content.find(key, start_pos);
        if (pos == std::string::npos) {
            return 0;
        }
        
        size_t val_start = pos + key.length();
        while (val_start < content.size() && ((std::isspace(content[val_start]) != 0) || content[val_start] == ':')) {
            val_start++;
        }
        
        size_t val_end = val_start;
        while (val_end < content.size() && (std::isdigit(content[val_end]) != 0)) {
            val_end++;
        }
        
        if (val_start == val_end) {
            return 0;
        }
        return std::stoi(content.substr(val_start, val_end - val_start));
    }

    static size_t find_closing_bracket(const std::string& s, size_t start, char open, char close) {
        int depth = 0;
        for (size_t i = start; i < s.size(); i++) {
            if (s[i] == open) { depth++;
            } else if (s[i] == close) {
                depth--;
                if (depth == 0) { return i;
}
            }
        }
        return std::string::npos;
    }

    bool unpack_to_dir() {
        if (!fs::exists(config_.output_dir)) {
            std::error_code ec;
            if (!fs::create_directories(config_.output_dir, ec)) {
                std::cerr << "Error: Failed to create output directory " << to_quoted(config_.output_dir.string()) << "\n";
                return false;
            }
        }

        std::cout << "Unpacking " << frames_.size() << " frames to " << to_quoted(config_.output_dir.string()) << "...\n";

        for (const auto& frame : frames_) {
            if (!save_sprite_image(frame)) {
                std::cerr << "Warning: Failed to save sprite " << to_quoted(frame.name) << "\n";
            }
        }

        return true;
    }

    bool unpack_to_stdout() {
        // Output as a tar archive to stdout
        struct archive* a = archive_write_new();
        if (a == nullptr) {
            std::cerr << "Error: Failed to create archive writer\n";
            return false;
        }

        if (archive_write_set_format_pax_restricted(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to set archive format: " << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        if (archive_write_add_filter_none(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to set compression: " << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        // Use a callback to write to stdout directly
        std::vector<unsigned char> archive_buffer;
        auto write_callback = [](struct archive* /*unused*/, void* /*client_data*/, const void* buffer, size_t length) -> la_ssize_t {
            std::cout.write(static_cast<const char*>(buffer), length);
            if (std::cout.fail()) { return -1;
}
            return length;
        };

        if (archive_write_open(a, nullptr, nullptr, write_callback, nullptr) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to open memory for archive: " << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        for (const auto& frame : frames_) {
            if (!write_sprite_to_archive_entry(a, frame)) {
                std::cerr << "Warning: Failed to add sprite " << to_quoted(frame.name) << " to archive\n";
                archive_write_free(a);
                return false;
            }
        }

        if (archive_write_close(a) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to close archive: " << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        archive_write_free(a);
        return true;
    }

    static bool unpack_to_tar(const fs::path& /*tar_path*/) {
        // Not implemented for now, similar to stdout
        return false;
    }

    bool write_sprite_to_archive_entry(struct archive* a, const SpriteFrame& frame) {
        const auto& bounds = frame.frame;
        const int out_w = frame.rotated ? bounds.h : bounds.w;
        const int out_h = frame.rotated ? bounds.w : bounds.h;
        
        std::vector<unsigned char> sprite_data(static_cast<size_t>(out_w) * out_h * NUM_CHANNELS);
        for (int oy = 0; oy < out_h; oy++) {
            for (int ox = 0; ox < out_w; ox++) {
                int atlas_x = 0;
                int atlas_y = 0;
                if (frame.rotated) {
                    atlas_x = bounds.x + (out_h - 1 - oy);
                    atlas_y = bounds.y + ox;
                } else {
                    atlas_x = bounds.x + ox;
                    atlas_y = bounds.y + oy;
                }

                const size_t dst_idx = (static_cast<size_t>(oy) * out_w + ox) * NUM_CHANNELS;
                const size_t src_idx = (static_cast<size_t>(atlas_y) * width_ + atlas_x) * NUM_CHANNELS;
                
                std::memcpy(&sprite_data[dst_idx], &image_data_[src_idx], NUM_CHANNELS);
            }
        }

        // Encode as PNG in memory
        int png_size = 0;
        unsigned char* png_buffer_raw = stbi_write_png_to_mem(sprite_data.data(), out_w * NUM_CHANNELS,
                                                       out_w, out_h, NUM_CHANNELS, &png_size);
        if (png_buffer_raw == nullptr) {
            return false;
        }
        std::unique_ptr<unsigned char, void(*)(void*)> png_buffer(png_buffer_raw, std::free);

        std::string filename = frame.name;
        if (filename.find('.') == std::string::npos) {
            filename += ".png";
        }

        struct archive_entry* entry = archive_entry_new();
        if (entry == nullptr) {
            return false;
        }

        archive_entry_set_pathname(entry, filename.c_str());
        archive_entry_set_size(entry, png_size);
        archive_entry_set_filetype(entry, AE_IFREG);
        constexpr int DEFAULT_FILE_PERMISSIONS = 0644;
        archive_entry_set_perm(entry, DEFAULT_FILE_PERMISSIONS);
        archive_entry_set_mtime(entry, time(nullptr), 0);

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            std::cerr << "Error: Failed to write archive header: " << archive_error_string(a) << '\n';
            archive_entry_free(entry);
            return false;
        }

        if (archive_write_data(a, png_buffer.get(), png_size) != static_cast<ssize_t>(png_size)) {
            std::cerr << "Error: Failed to write archive data: " << archive_error_string(a) << '\n';
            archive_entry_free(entry);
            return false;
        }

        archive_entry_free(entry);
        return true;
    }

    bool save_sprite_image(const SpriteFrame& frame) {
        const auto& bounds = frame.frame;
        const int out_w = frame.rotated ? bounds.h : bounds.w;
        const int out_h = frame.rotated ? bounds.w : bounds.h;
        
        std::vector<unsigned char> sprite_data(static_cast<size_t>(out_w) * out_h * NUM_CHANNELS);
        for (int oy = 0; oy < out_h; oy++) {
            for (int ox = 0; ox < out_w; ox++) {
                int atlas_x = 0;
                int atlas_y = 0;
                if (frame.rotated) {
                    atlas_x = bounds.x + (out_h - 1 - oy);
                    atlas_y = bounds.y + ox;
                } else {
                    atlas_x = bounds.x + ox;
                    atlas_y = bounds.y + oy;
                }

                const size_t dst_idx = (static_cast<size_t>(oy) * out_w + ox) * NUM_CHANNELS;
                const size_t src_idx = (static_cast<size_t>(atlas_y) * width_ + atlas_x) * NUM_CHANNELS;
                
                std::memcpy(&sprite_data[dst_idx], &image_data_[src_idx], NUM_CHANNELS);
            }
        }

        fs::path output_path = config_.output_dir / frame.name;
        if (output_path.extension().empty()) {
            output_path += ".png";
        }
        
        fs::create_directories(output_path.parent_path());

        return stbi_write_png(output_path.string().c_str(),
                             out_w, out_h, NUM_CHANNELS,
                             sprite_data.data(), out_w * NUM_CHANNELS) != 0;
    }
};

void print_usage() {
    std::cout << "Usage: spratunpack [atlas.png|-] [OPTIONS]\n"
              << "\n"
              << "Extract individual sprites from an atlas using a frames definition file.\n"
              << "If atlas path is omitted or '-' is used, atlas PNG is read from stdin.\n"
              << "\n"
              << "Options:\n"
              << "  -f, --frames PATH          Frames definition file (or '-' for stdin)\n"
              << "  -o, --output DIR           Output directory (if omitted, output as TAR to stdout)\n"
              << "  -j, --threads N            Number of threads to use (default: auto)\n"
              << "  -h, --help                 Show this help message\n";
}

} // namespace

int run_spratunpack(int argc, char** argv) {
    Config config;
    config.output_dir = "";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "-") {
            if (config.input_path.empty() && !config.input_from_stdin) {
                config.input_from_stdin = true;
            } else {
                std::cerr << "Error: Too many arguments: " << arg << "\n";
                print_usage();
                return 1;
            }
        } else if (arg == "-f" || arg == "--frames") {
            if (i + 1 < argc) {
                config.frames_path = argv[++i];
                if (config.frames_path == "-") {
                    config.frames_from_stdin = true;
                    config.frames_path = "";
                }
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.output_dir = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "-j" || arg == "--threads") {
            if (i + 1 < argc) {
                if (!parse_non_negative_uint(argv[++i], config.threads)) {
                    std::cerr << "Error: Invalid thread count: " << argv[i] << "\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg.starts_with("-")) {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage();
            return 1;
        } else {
            if (config.input_path.empty()) {
                config.input_path = arg;
            } else {
                std::cerr << "Error: Too many arguments: " << arg << "\n";
                print_usage();
                return 1;
            }
        }
    }

    if (config.input_path.empty() && !config.input_from_stdin) {
        config.input_from_stdin = true;
    }

    if (config.input_from_stdin && config.frames_from_stdin) {
        std::cerr << "Error: Cannot read both atlas image and frames from stdin.\n";
        return 1;
    }

    if (config.output_dir.empty()) {
        config.stdout_mode = true;
    }

    if (config.threads == 0) {
        config.threads = std::max(1U, std::thread::hardware_concurrency());
    }

    // Initialize binary mode for stdout if needed
#ifdef _WIN32
    if (config.input_from_stdin) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    if (config.stdout_mode) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    SpriteUnpacker unpacker(config);
    if (!unpacker.run()) {
        return 1;
    }

    return 0;
}
