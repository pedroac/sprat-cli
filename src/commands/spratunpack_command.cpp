// spratunpack.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++20 -O2 src/spratunpack.cpp -o spratunpack

#include <algorithm>
#include <atomic>
#include <charconv>
#include <iostream>
#include <utility>
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
namespace fs = std::filesystem;
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <iterator>
#include "core/cli_parse.h"
#include "core/layout_parser.h"

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

#include <stb_image.h>
#include <stb_image_write.h>

// Libarchive for proper tar format
#include <archive.h>
#include <json.hpp>
#include <archive_entry.h>
#include "core/i18n.h"

namespace {
using sprat::core::parse_non_negative_uint;
using sprat::core::tr;
using sprat::core::to_quoted;

constexpr int NUM_CHANNELS = 4;

struct Rectangle {
    int x, y, w, h;
};

// Every frames format is normalized to this: where the pixels sit in the atlas, and where
// they go in the sprite's original canvas.
struct SpriteFrame {
    std::string name;
    Rectangle frame{};   // region in the atlas, in packed orientation (w/h swapped if rotated)
    bool rotated{};
    bool rotated_ccw{};  // direction of the rotation in the atlas (layouts with "rotation ccw")
    int canvas_w{}, canvas_h{};   // original sprite size; 0 means the unrotated content size
    int offset_x{}, offset_y{};   // content position inside the canvas
    int atlas_index{};            // atlas of a layout sprite (multipack layouts)

    [[nodiscard]] int content_w() const { return rotated ? frame.h : frame.w; }
    [[nodiscard]] int content_h() const { return rotated ? frame.w : frame.h; }
};

struct Config {
    fs::path input_path;
    fs::path detected_input_path;
    fs::path frames_path;
    fs::path output_dir;
    bool input_from_stdin = false;
    bool frames_from_stdin = false;
    bool stdout_mode = false;
    bool keep_trim = false;
    int atlas_index = -1;  // atlas of a multipack layout to unpack; -1 picks it automatically
    bool debug = false;
    unsigned int threads = 0;
};

class SpriteUnpacker {
public:
    SpriteUnpacker(Config  config) : config_(std::move(config)) {}

    bool run() {
        if (!load_frames()) {
            return false;
        }
        if (!load_image()) {
            return false;
        }
        if (!select_layout_atlas()) {
            return false;
        }
        // Check every frame before writing anything, so a bad frames file leaves no partial output.
        if (!validate_frames()) {
            return false;
        }
        if (config_.stdout_mode) {
            return unpack_to_stdout();
        }
        return unpack_to_dir();
    }

private:
    Config config_;
    int width_{}, height_{}, channels_{};
    std::vector<unsigned char> image_data_;
    std::vector<SpriteFrame> frames_;
    std::vector<sprat::core::Atlas> layout_atlases_;  // atlases of a layout frames file

    bool load_image() {
        std::vector<unsigned char> buffer;
        fs::path actual_input_path = config_.input_path;
        if (actual_input_path.empty() && !config_.detected_input_path.empty()) {
            actual_input_path = resolve_detected_path(config_.detected_input_path);
        }

        if (actual_input_path.empty() && config_.input_from_stdin) {
            buffer.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        } else if (!actual_input_path.empty()) {
            std::ifstream in(actual_input_path, std::ios::binary);
            if (in) {
                buffer.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        } else {
            std::cerr << tr("Error: No input atlas provided.\n");
            return false;
        }

        if (buffer.empty()) {
            std::cerr << tr("Error: No atlas image data received\n");
            return false;
        }

        // Check for obfuscation
        const std::string prefix = "SPRAT!";
        const std::string key = "sprat";
        if (buffer.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), buffer.begin())) {
            std::vector<unsigned char> deobfuscated;
            deobfuscated.reserve(buffer.size() - prefix.size());
            for (size_t i = prefix.size(); i < buffer.size(); ++i) {
                deobfuscated.push_back(buffer[i] ^ static_cast<unsigned char>(key[(i - prefix.size()) % key.size()]));
            }
            buffer = std::move(deobfuscated);
        }

        unsigned char* data = stbi_load_from_memory(
            buffer.data(),
            static_cast<int>(buffer.size()),
            &width_,
            &height_,
            &channels_,
            NUM_CHANNELS);

        if (data == nullptr) {
            std::cerr << tr("Error: Failed to load atlas image (Reason: ") << stbi_failure_reason() << tr(")\n");
            return false;
        }

        image_data_.assign(data, data + (static_cast<size_t>(width_) * height_ * NUM_CHANNELS));
        stbi_image_free(data);
        return true;
    }

    // spratframes writes the "path" line as given on its command line, so a relative path is
    // relative to the directory it ran in, which is usually also where spratunpack runs. When
    // it isn't there, try next to the frames file (a frames file moved along with its sheet).
    [[nodiscard]] fs::path resolve_detected_path(const fs::path& path) const {
        if (path.is_absolute() || config_.frames_path.empty()) {
            return path;
        }
        std::error_code ec;
        if (fs::exists(path, ec)) {
            return path;
        }
        const fs::path beside_frames = config_.frames_path.parent_path() / path;
        return fs::exists(beside_frames, ec) ? beside_frames : path;
    }

    bool load_frames() {
        std::string content;
        std::string extension;

        if (config_.frames_from_stdin || (config_.frames_path.empty() && config_.input_from_stdin)) {
            content.assign(std::istreambuf_iterator<char>(std::cin),
                          std::istreambuf_iterator<char>());
            if (content.empty()) {
                if (config_.frames_from_stdin) {
                    std::cerr << tr("Error: No frames data received on stdin\n");
                    return false;
                }
                // If we were just hoping for frames on stdin because input was missing,
                // but got nothing, we'll fail later in load_image.
                // But wait, if we are here, input_from_stdin is true.
                std::cerr << tr("Error: No data received on stdin. Expected atlas image or frames definition.\n");
                return false;
            }
            // For stdin, we try to detect format from content
            if (looks_like_json(content)) {
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
                        std::cerr << tr("Error: No data received on stdin. Expected frames definition.\n");
                        return false;
                    }
                    if (looks_like_json(content)) {
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
                                if (looks_like_json(content)) {
                                    extension = ".json";
                                } else {
                                    extension = ".spratframes";
                                }
                            } else {
                                std::cerr << tr("Error: Frames file not found and could not be auto-detected.\n");
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
                    std::cerr << tr("Error: Failed to open frames file ") << to_quoted(config_.frames_path.string()) << "\n";
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
        if (looks_like_json(content)) {
            if (parse_json(content)) {
                return true;
            }
        }

        if (parse_spratframes(content)) {
            return true;
        }

        std::cerr << tr("Error: Unsupported frames format ") << extension << tr(" and could not auto-detect format from content.\n");
        return false;
    }

    bool parse_spratframes(const std::string& content) {
        if (is_layout(content)) {
            return parse_layout_frames(content);
        }
        std::istringstream iss(content);
        std::string line;
        int index = 0;
        bool rotate_ccw = false;
        double scale = 1.0;
        std::vector<sprat::core::Sprite> layout_sprites;
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

            if (line.starts_with("rotation ")) {
                if (!sprat::core::parse_rotation_line(line, rotate_ccw)) {
                    std::cerr << tr("Error: Invalid rotation line (expected cw or ccw): ") << line << "\n";
                    return false;
                }
                continue;
            }

            if (line.starts_with("scale ")) {
                if (!sprat::core::parse_scale_line(line, scale)) {
                    std::cerr << tr("Error: Invalid scale line: ") << line << "\n";
                    return false;
                }
                continue;
            }

            if (!line.starts_with("sprite ")) {
                continue;
            }

            size_t pos = 7;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
                pos++;
            }
            if (pos < line.size() && line[pos] == '"') {
                // Layout line: named, with optional trims, rotation and slices.
                sprat::core::Sprite sprite;
                std::string error;
                if (!sprat::core::parse_sprite_line(line, sprite, error)) {
                    std::cerr << tr("Error: Invalid sprite line: ") << line << " (" << error << ")\n";
                    return false;
                }
                layout_sprites.push_back(std::move(sprite));
                continue;
            }

            // spratframes line: "sprite x,y w,h [rotated]", named by position.
            std::vector<std::string> tokens;
            std::istringstream liss(line.substr(pos));
            std::string token;
            while (liss >> token) {
                tokens.push_back(token);
            }
            SpriteFrame frame;
            if (tokens.size() < 2 ||
                !sprat::core::parse_pair(tokens[0], frame.frame.x, frame.frame.y) ||
                !sprat::core::parse_pair(tokens[1], frame.frame.w, frame.frame.h)) {
                std::cerr << tr("Error: Invalid sprite line: ") << line << "\n";
                return false;
            }
            frame.name = "sprite_" + std::to_string(index++);
            frame.rotated = std::ranges::find(tokens, "rotated") != tokens.end();
            frames_.push_back(frame);
        }
        for (auto& frame : frames_) {
            frame.rotated_ccw = rotate_ccw;
        }
        for (const auto& sprite : layout_sprites) {
            if (!add_layout_frame(sprite, scale, rotate_ccw)) {
                return false;
            }
        }
        return !frames_.empty();
    }

    // A frame from a layout sprite. Layout trims are in original-image pixels (see
    // metadata_trim); the canvas is the original image at the layout's scale.
    bool add_layout_frame(const sprat::core::Sprite& sprite, double scale, bool rotate_ccw) {
        SpriteFrame frame;
        frame.name = sprite.path;
        frame.frame = {.x = sprite.x, .y = sprite.y, .w = sprite.w, .h = sprite.h};
        frame.rotated = sprite.rotated;
        frame.rotated_ccw = rotate_ccw;
        frame.atlas_index = sprite.atlas_index;
        const sprat::core::MetadataTrim trim = sprat::core::metadata_trim(sprite, scale);
        const long long canvas_w = static_cast<long long>(frame.content_w()) + trim.left + trim.right;
        const long long canvas_h = static_cast<long long>(frame.content_h()) + trim.top + trim.bottom;
        if (trim.left < 0 || trim.top < 0 || trim.right < 0 || trim.bottom < 0 ||
            canvas_w > std::numeric_limits<int>::max() || canvas_h > std::numeric_limits<int>::max()) {
            std::cerr << tr("Error: Invalid trim for sprite ") << to_quoted(sprite.path) << "\n";
            return false;
        }
        frame.canvas_w = static_cast<int>(canvas_w);
        frame.canvas_h = static_cast<int>(canvas_h);
        frame.offset_x = trim.left;
        frame.offset_y = trim.top;
        frames_.push_back(std::move(frame));
        return true;
    }

    // A spratlayout layout (it has "atlas" lines): parsed by the shared layout parser, so
    // aliases of deduplicated sprites are unpacked too and every sprite knows its atlas.
    bool parse_layout_frames(const std::string& content) {
        std::istringstream in(content);
        sprat::core::Layout layout;
        std::string error;
        if (!sprat::core::parse_layout(in, layout, error) ||
            !sprat::core::expand_layout_aliases(layout, error)) {
            std::cerr << tr("Error: Invalid layout: ") << error << "\n";
            return false;
        }
        layout_atlases_ = layout.atlases;
        for (const auto& sprite : layout.sprites) {
            if (!add_layout_frame(sprite, layout.scale, layout.rotate_ccw)) {
                return false;
            }
        }
        return !frames_.empty();
    }

    static bool is_layout(const std::string& content) {
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line)) {
            if (trim_copy(line).starts_with("atlas ")) {
                return true;
            }
        }
        return false;
    }

    // A layout may describe several atlases (multipack) while one image is unpacked: keep the
    // sprites of that image's atlas. It is given by --atlas-index, or it is the only atlas
    // of the layout, or the only one with the image's size.
    bool select_layout_atlas() {
        if (layout_atlases_.empty()) {
            return true;
        }
        int index = config_.atlas_index;
        if (index >= 0) {
            if (std::cmp_greater_equal(index, layout_atlases_.size())) {
                std::cerr << tr("Error: --atlas-index ") << index << tr(" is out of range; the layout has ")
                          << layout_atlases_.size() << tr(" atlases\n");
                return false;
            }
        } else if (layout_atlases_.size() == 1) {
            index = 0;
        } else {
            std::vector<int> matches;
            for (size_t i = 0; i < layout_atlases_.size(); ++i) {
                if (layout_atlases_[i].width == width_ && layout_atlases_[i].height == height_) {
                    matches.push_back(static_cast<int>(i));
                }
            }
            if (matches.size() != 1) {
                std::cerr << tr("Error: The layout has ") << layout_atlases_.size()
                          << tr(" atlases; use --atlas-index to choose the one this image is\n");
                return false;
            }
            index = matches.front();
        }
        std::erase_if(frames_, [index](const SpriteFrame& frame) { return frame.atlas_index != index; });
        if (frames_.empty()) {
            std::cerr << tr("Error: No sprites in atlas ") << index << "\n";
            return false;
        }
        return true;
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

    // Frame names become file paths (on disk or inside the TAR), so they must stay
    // relative and must not climb out of the output location (path traversal guard).
    static bool check_frame_name(const std::string& name) {
        const fs::path name_path(name);
        if (name_path.has_root_path()) {
            std::cerr << tr("Error: Frame name is an absolute path: ") << to_quoted(name) << "\n";
            return false;
        }
        const fs::path normalized = name_path.lexically_normal();
        if (normalized.empty() || normalized.begin()->string() == "..") {
            std::cerr << tr("Error: Frame name escapes output directory: ") << to_quoted(name) << "\n";
            return false;
        }
        return true;
    }

    // Reads a JSON number as int; TexturePacker-style files may store coordinates as floats.
    // Missing keys read as 0; values that are not numbers or do not fit an int are refused.
    static bool json_int(const nlohmann::json& obj, const char* key, int& out) {
        out = 0;
        const auto it = obj.find(key);
        if (it == obj.end()) {
            return true;
        }
        if (!it->is_number()) {
            return false;
        }
        constexpr double k_min = std::numeric_limits<int>::min();
        constexpr double k_max = std::numeric_limits<int>::max();
        if (it->is_number_float()) {
            const double v = std::round(it->get<double>());
            if (!std::isfinite(v) || v < k_min || v > k_max) {
                return false;
            }
            out = static_cast<int>(v);
            return true;
        }
        if (it->is_number_unsigned()) {
            const auto v = it->get<std::uint64_t>();
            if (v > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            out = static_cast<int>(v);
            return true;
        }
        const auto v = it->get<std::int64_t>();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }

    static bool json_rect(const nlohmann::json& obj, const char* key, Rectangle& out, bool& present) {
        out = {.x = 0, .y = 0, .w = 0, .h = 0};
        const auto it = obj.find(key);
        present = it != obj.end() && it->is_object();
        if (!present) {
            return true;
        }
        return json_int(*it, "x", out.x) && json_int(*it, "y", out.y) &&
               json_int(*it, "w", out.w) && json_int(*it, "h", out.h);
    }

    static bool json_bool(const nlohmann::json& obj, const char* key) {
        const auto it = obj.find(key);
        return it != obj.end() && it->is_boolean() && it->get<bool>();
    }

    // TexturePacker-style frame. "frame" holds the unrotated size even for rotated frames
    // (the atlas region is h x w then); "sourceSize" and "spriteSourceSize" place the
    // trimmed pixels in the original canvas.
    static bool json_frame(const std::string& name, const nlohmann::json& obj, SpriteFrame& frame) {
        frame = {};
        frame.name = name;
        frame.rotated = json_bool(obj, "rotated");
        Rectangle rect{};
        Rectangle sprite_source{};
        bool has_rect = false;
        bool has_sprite_source = false;
        if (!json_rect(obj, "frame", rect, has_rect) ||
            !json_rect(obj, "spriteSourceSize", sprite_source, has_sprite_source)) {
            std::cerr << tr("Error: Frame ") << to_quoted(name) << tr(" has invalid coordinates\n");
            return false;
        }
        frame.frame = rect;
        if (frame.rotated) {
            std::swap(frame.frame.w, frame.frame.h);
        }
        const auto source_size = obj.find("sourceSize");
        if (source_size != obj.end() && source_size->is_object()) {
            if (!json_int(*source_size, "w", frame.canvas_w) || !json_int(*source_size, "h", frame.canvas_h)) {
                std::cerr << tr("Error: Frame ") << to_quoted(name) << tr(" has invalid coordinates\n");
                return false;
            }
        }
        if (has_sprite_source) {
            frame.offset_x = sprite_source.x;
            frame.offset_y = sprite_source.y;
        }
        return true;
    }

    bool parse_json_frames(const nlohmann::json& frames) {
        SpriteFrame frame;
        if (frames.is_object()) {
            for (const auto& [name, obj] : frames.items()) {
                if (obj.is_object()) {
                    if (!json_frame(name, obj, frame)) {
                        return false;
                    }
                    frames_.push_back(frame);
                }
            }
        } else if (frames.is_array()) {
            for (const auto& obj : frames) {
                if (!obj.is_object()) {
                    continue;
                }
                const auto filename = obj.find("filename");
                if (filename != obj.end() && filename->is_string()) {
                    if (!json_frame(filename->get<std::string>(), obj, frame)) {
                        return false;
                    }
                    frames_.push_back(frame);
                }
            }
        }
        return true;
    }

    // Phaser multi-atlas JSON lists one entry per texture; unpack the one for this atlas:
    // the entry whose image file name matches the atlas file, or the only entry.
    const nlohmann::json* select_texture(const nlohmann::json& textures) const {
        const fs::path atlas = !config_.input_path.empty() ? config_.input_path : config_.detected_input_path;
        std::vector<std::string> images;
        for (const auto& texture : textures) {
            if (!texture.is_object()) {
                continue;
            }
            const auto image = texture.find("image");
            const std::string image_name = (image != texture.end() && image->is_string())
                ? image->get<std::string>() : std::string();
            if (!atlas.empty() && fs::path(image_name).filename() == atlas.filename()) {
                return &texture;
            }
            images.push_back(image_name);
        }
        if (textures.size() == 1 && textures[0].is_object()) {
            return &textures[0];
        }
        std::cerr << tr("Error: Multi-atlas JSON has no texture for this atlas; its images are:");
        for (const auto& image : images) {
            std::cerr << " " << to_quoted(image);
        }
        std::cerr << "\n";
        return nullptr;
    }

    // Supports TexturePacker-style JSON: {"frames": {name: {...}}}, {"frames": [{"filename": ...}]},
    // Phaser multi-atlas JSON {"textures": [{"image": ..., "frames": [...]}]} or a bare array of
    // frames. Other top-level keys (such as "meta") are ignored.
    bool parse_json(const std::string& content) {
        const nlohmann::json root = nlohmann::json::parse(content, nullptr, false);
        if (root.is_discarded()) {
            std::cerr << tr("Error: Invalid JSON in frames file\n");
            return false;
        }

        const nlohmann::json* frames = &root;
        if (root.is_object()) {
            const auto textures = root.find("textures");
            const auto it = root.find("frames");
            if (it != root.end()) {
                frames = &*it;
            } else if (textures != root.end() && textures->is_array()) {
                const nlohmann::json* texture = select_texture(*textures);
                if (texture == nullptr) {
                    return false;
                }
                const auto texture_frames = texture->find("frames");
                if (texture_frames == texture->end()) {
                    std::cerr << tr("Error: Invalid JSON format (missing \"frames\")\n");
                    return false;
                }
                frames = &*texture_frames;
            } else {
                std::cerr << tr("Error: Invalid JSON format (missing \"frames\")\n");
                return false;
            }
        }

        return parse_json_frames(*frames) && !frames_.empty();
    }

    static bool looks_like_json(const std::string& content) {
        const size_t first = content.find_first_not_of(" \t\r\n");
        return first != std::string::npos && (content[first] == '{' || content[first] == '[');
    }

    // The file a frame is written to: its name, with ".png" added unless it already ends so
    // (names such as "walk.001" keep their dots).
    static std::string output_filename(const std::string& name) {
        std::string lower = name;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.ends_with(".png") ? name : name + ".png";
    }

    [[nodiscard]] int output_w(const SpriteFrame& frame) const {
        return config_.keep_trim ? frame.content_w() : frame.canvas_w;
    }
    [[nodiscard]] int output_h(const SpriteFrame& frame) const {
        return config_.keep_trim ? frame.content_h() : frame.canvas_h;
    }

    bool validate_frames() {
        std::vector<std::string> filenames;
        filenames.reserve(frames_.size());
        for (auto& frame : frames_) {
            const auto& r = frame.frame;
            // 64-bit so huge coordinates are refused instead of overflowing.
            const bool inside = r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 &&
                static_cast<long long>(r.x) + r.w <= width_ &&
                static_cast<long long>(r.y) + r.h <= height_;
            if (!inside) {
                std::cerr << tr("Error: Frame ") << to_quoted(frame.name) << tr(" references pixels outside the atlas bounds\n");
                return false;
            }
            if (frame.canvas_w <= 0 || frame.canvas_h <= 0) {
                frame.canvas_w = frame.content_w();
                frame.canvas_h = frame.content_h();
            }
            const bool fits = frame.offset_x >= 0 && frame.offset_y >= 0 &&
                static_cast<long long>(frame.offset_x) + frame.content_w() <= frame.canvas_w &&
                static_cast<long long>(frame.offset_y) + frame.content_h() <= frame.canvas_h;
            if (!fits) {
                std::cerr << tr("Error: Frame ") << to_quoted(frame.name) << tr(" does not fit its original size\n");
                return false;
            }
            if (!check_frame_name(frame.name)) {
                return false;
            }
            filenames.push_back(fs::path(output_filename(frame.name)).lexically_normal().generic_string());
        }
        std::vector<std::string> sorted = filenames;
        std::ranges::sort(sorted);
        const auto duplicate = std::ranges::adjacent_find(sorted);
        if (duplicate != sorted.end()) {
            std::cerr << tr("Error: Several frames would be written to ") << to_quoted(*duplicate) << "\n";
            return false;
        }
        return true;
    }

    // PNG encoding is the expensive part, so frames are encoded on --threads workers, a batch
    // at a time to bound memory, and written in frame order by the caller; the output is the
    // same as a single-threaded run.
    template <typename Write>
    bool encode_and_write(Write&& write) {
        const size_t workers = std::max<size_t>(1, config_.threads);
        const size_t batch_size = std::max<size_t>(64, workers * 8);
        std::vector<std::vector<unsigned char>> pngs;
        for (size_t begin = 0; begin < frames_.size(); begin += batch_size) {
            const size_t end = std::min(frames_.size(), begin + batch_size);
            pngs.assign(end - begin, {});
            std::atomic<size_t> next{begin};
            const auto encode = [&] {
                for (size_t i = next++; i < end; i = next++) {
                    pngs[i - begin] = encode_png(frames_[i]);
                }
            };
            std::vector<std::thread> threads;
            const size_t spawn = std::min(workers, end - begin) - 1;
            threads.reserve(spawn);
            for (size_t t = 0; t < spawn; ++t) {
                threads.emplace_back(encode);
            }
            encode();
            for (auto& thread : threads) {
                thread.join();
            }
            for (size_t i = begin; i < end; ++i) {
                if (pngs[i - begin].empty()) {
                    std::cerr << tr("Error: Failed to encode sprite ") << to_quoted(frames_[i].name) << "\n";
                    return false;
                }
                if (!write(frames_[i], pngs[i - begin])) {
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<unsigned char> encode_png(const SpriteFrame& frame) {
        const int out_w = output_w(frame);
        const int out_h = output_h(frame);
        const std::vector<unsigned char> sprite_data = extract_sprite_pixels(frame);
        std::vector<unsigned char> png;
        auto write_to_vec = [](void* context, void* data, int size) {
            auto* vec = static_cast<std::vector<unsigned char>*>(context);
            const auto* bytes = static_cast<const unsigned char*>(data);
            vec->insert(vec->end(), bytes, bytes + size);
        };
        if (!stbi_write_png_to_func(write_to_vec, &png, out_w, out_h, NUM_CHANNELS, sprite_data.data(), out_w * NUM_CHANNELS)) {
            png.clear();
        }
        return png;
    }

    bool unpack_to_dir() {
        if (!fs::exists(config_.output_dir)) {
            std::error_code ec;
            if (!fs::create_directories(config_.output_dir, ec)) {
                std::cerr << tr("Error: Failed to create output directory ") << to_quoted(config_.output_dir.string()) << "\n";
                return false;
            }
        }

        std::cout << tr("Unpacking ") << frames_.size() << tr(" frames to ") << to_quoted(config_.output_dir.string()) << tr("...\n");

        return encode_and_write([&](const SpriteFrame& frame, const std::vector<unsigned char>& png) {
            if (!save_png(frame, png)) {
                std::cerr << tr("Error: Failed to save sprite ") << to_quoted(frame.name) << "\n";
                return false;
            }
            return true;
        });
    }

    bool unpack_to_stdout() {
        // Output as a tar archive to stdout
        struct archive* a = archive_write_new();
        if (a == nullptr) {
            std::cerr << tr("Error: Failed to create archive writer\n");
            return false;
        }

        if (archive_write_set_format_pax_restricted(a) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to set archive format: ") << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        if (archive_write_add_filter_none(a) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to set compression: ") << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        // Use a callback to write to stdout directly
        auto write_callback = [](struct archive* /*unused*/, void* /*client_data*/, const void* buffer, size_t length) -> la_ssize_t {
            std::cout.write(static_cast<const char*>(buffer), length);
            if (std::cout.fail()) { return -1;
}
            return length;
        };

        if (archive_write_open(a, nullptr, nullptr, write_callback, nullptr) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to open memory for archive: ") << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        const bool written = encode_and_write([&](const SpriteFrame& frame, const std::vector<unsigned char>& png) {
            if (!write_archive_entry(a, frame, png)) {
                std::cerr << tr("Error: Failed to add sprite ") << to_quoted(frame.name) << tr(" to archive\n");
                return false;
            }
            return true;
        });
        if (!written) {
            archive_write_free(a);
            return false;
        }

        if (archive_write_close(a) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to close archive: ") << archive_error_string(a) << '\n';
            archive_write_free(a);
            return false;
        }

        archive_write_free(a);
        return true;
    }

    // The sprite's pixels, rotation undone, placed at their offset in the original canvas
    // (or just the trimmed pixels with --keep-trim). Frames are validated beforehand.
    std::vector<unsigned char> extract_sprite_pixels(const SpriteFrame& frame) {
        const auto& bounds = frame.frame;
        const int content_w = frame.content_w();
        const int content_h = frame.content_h();
        const int out_w = output_w(frame);
        const int out_h = output_h(frame);
        const int ox0 = config_.keep_trim ? 0 : frame.offset_x;
        const int oy0 = config_.keep_trim ? 0 : frame.offset_y;

        std::vector<unsigned char> sprite_data(static_cast<size_t>(out_w) * out_h * NUM_CHANNELS, 0);
        if (!frame.rotated) {
            const size_t row_bytes = static_cast<size_t>(content_w) * NUM_CHANNELS;
            for (int cy = 0; cy < content_h; cy++) {
                const size_t dst_offset = (static_cast<size_t>(oy0 + cy) * out_w + ox0) * NUM_CHANNELS;
                const size_t src_offset = (static_cast<size_t>(bounds.y + cy) * width_ + bounds.x) * NUM_CHANNELS;
                std::memcpy(&sprite_data[dst_offset], &image_data_[src_offset], row_bytes);
            }
        } else {
            // Undo the packing rotation: CW maps source (cx, cy) to atlas (h-1-cy, cx),
            // CCW maps it to atlas (cy, w-1-cx).
            for (int cy = 0; cy < content_h; cy++) {
                for (int cx = 0; cx < content_w; cx++) {
                    const int atlas_x = bounds.x + (frame.rotated_ccw ? cy : content_h - 1 - cy);
                    const int atlas_y = bounds.y + (frame.rotated_ccw ? content_w - 1 - cx : cx);
                    const size_t dst_idx = (static_cast<size_t>(oy0 + cy) * out_w + ox0 + cx) * NUM_CHANNELS;
                    const size_t src_idx = (static_cast<size_t>(atlas_y) * width_ + atlas_x) * NUM_CHANNELS;
                    std::memcpy(&sprite_data[dst_idx], &image_data_[src_idx], NUM_CHANNELS);
                }
            }
        }
        return sprite_data;
    }

    bool write_archive_entry(struct archive* a, const SpriteFrame& frame, const std::vector<unsigned char>& png) {
        const std::string filename = output_filename(frame.name);

        struct archive_entry* entry = archive_entry_new();
        if (entry == nullptr) {
            return false;
        }

        archive_entry_set_pathname(entry, filename.c_str());
        archive_entry_set_size(entry, static_cast<la_int64_t>(png.size()));
        archive_entry_set_filetype(entry, AE_IFREG);
        constexpr int DEFAULT_FILE_PERMISSIONS = 0644;
        archive_entry_set_perm(entry, DEFAULT_FILE_PERMISSIONS);
        archive_entry_set_mtime(entry, time(nullptr), 0);

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to write archive header: ") << archive_error_string(a) << '\n';
            archive_entry_free(entry);
            return false;
        }

        if (archive_write_data(a, png.data(), png.size()) != static_cast<ssize_t>(png.size())) {
            std::cerr << tr("Error: Failed to write archive data: ") << archive_error_string(a) << '\n';
            archive_entry_free(entry);
            return false;
        }

        archive_entry_free(entry);
        return true;
    }

    bool save_png(const SpriteFrame& frame, const std::vector<unsigned char>& png) const {
        const fs::path output_path = (config_.output_dir / fs::path(output_filename(frame.name))).lexically_normal();

        std::error_code ec;
        fs::create_directories(output_path.parent_path(), ec);
        if (ec) {
            std::cerr << tr("Error: Failed to create output directory for ") << to_quoted(frame.name) << "\n";
            return false;
        }

        std::ofstream out(output_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        return static_cast<bool>(out);
    }
};

void print_usage() {
    std::cout << tr("Usage: spratunpack [atlas.png|-] [OPTIONS]\n")
              << tr("\n")
              << tr("Extract individual sprites from an atlas using a frames definition file.\n")
              << tr("If atlas path is omitted or '-' is used, atlas PNG is read from stdin.\n")
              << tr("\n")
              << tr("Options:\n")
              << tr("  -f, --frames PATH          Frames definition file (or '-' for stdin)\n")
              << tr("  -o, --output DIR           Output directory (if omitted, output as TAR to stdout)\n")
              << tr("  --keep-trim                Write only the packed pixels instead of restoring the original size\n")
              << tr("  --atlas-index N            Atlas of a multipack layout that the image is (default: detected)\n")
              << tr("  -j, --threads N            Number of threads to use (default: auto)\n")
              << tr("  --debug                    Enable detailed error reporting\n")
              << tr("  -h, --help                 Show this help message\n")
              << tr("  -v, --version              Show version\n");
}

} // namespace

int run_spratunpack(int argc, char** argv) {
    Config config;
    config.output_dir = "";
    config.debug = (std::getenv("SPRAT_DEBUG") != nullptr);

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << tr("spratunpack version ") << SPRAT_VERSION << "\n";
            return 0;
        } else if (arg == "--debug") {
            config.debug = true;
        } else if (arg == "--keep-trim") {
            config.keep_trim = true;
        } else if (arg == "--atlas-index") {
            unsigned int index = 0;
            if (i + 1 >= argc || !parse_non_negative_uint(argv[++i], index) ||
                index > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
                std::cerr << tr("Error: Invalid atlas index\n");
                return 1;
            }
            config.atlas_index = static_cast<int>(index);
        } else if (arg == "-") {
            if (config.input_path.empty() && !config.input_from_stdin) {
                config.input_from_stdin = true;
            } else {
                std::cerr << tr("Error: Too many arguments: ") << arg << "\n";
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
                std::cerr << tr("Error: Missing value for ") << arg << "\n";
                return 1;
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.output_dir = argv[++i];
            } else {
                std::cerr << tr("Error: Missing value for ") << arg << "\n";
                return 1;
            }
        } else if (arg == "-j" || arg == "--threads") {
            if (i + 1 < argc) {
                if (!parse_non_negative_uint(argv[++i], config.threads)) {
                    std::cerr << tr("Error: Invalid thread count: ") << argv[i] << "\n";
                    return 1;
                }
            } else {
                std::cerr << tr("Error: Missing value for ") << arg << "\n";
                return 1;
            }
        } else if (arg.starts_with("-")) {
            std::cerr << tr("Unknown option: ") << arg << "\n";
            print_usage();
            return 1;
        } else {
            if (config.input_path.empty()) {
                config.input_path = arg;
            } else {
                std::cerr << tr("Error: Too many arguments: ") << arg << "\n";
                print_usage();
                return 1;
            }
        }
    }

    if (config.input_path.empty() && !config.input_from_stdin) {
        config.input_from_stdin = true;
    }

    if (config.input_from_stdin && config.frames_from_stdin) {
        std::cerr << tr("Error: Cannot read both atlas image and frames from stdin.\n");
        return 1;
    }

    if (config.output_dir.empty()) {
        config.stdout_mode = true;
    }

    if (config.threads == 0) {
        config.threads = std::max(1U, std::thread::hardware_concurrency());
    }
#ifdef __EMSCRIPTEN__
    config.threads = 1;
#endif

    // Initialize binary mode for stdin/stdout if needed
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
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
