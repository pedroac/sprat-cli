// spratlayout.cpp
// MIT License (c) 2026 Pedro
// Compile: g++ -std=c++17 -O2 src/spratlayout.cpp -o spratlayout

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <functional>
#include <memory>
#include <limits>
#include <cmath>
#include <utility>
#include <array>
#include <cstddef>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <system_error>
#include <archive.h>
#include <archive_entry.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;
constexpr int k_output_cache_format_version = 2;
constexpr int k_seed_cache_format_version = 2;
#ifndef SPRAT_GLOBAL_PROFILE_CONFIG
#define SPRAT_GLOBAL_PROFILE_CONFIG "/usr/local/share/sprat/spratprofiles.cfg"
#endif

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

enum class Mode { POT, COMPACT, FAST };
enum class OptimizeTarget { GPU, SPACE };
enum class ResolutionReference { Largest, Smallest };

struct ProfileDefinition {
    std::string name;
    Mode mode = Mode::COMPACT;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    std::optional<int> max_width;
    std::optional<int> max_height;
    std::optional<int> padding;
    std::optional<int> max_combinations;
    std::optional<double> scale;
    std::optional<bool> trim_transparent;
    std::optional<unsigned int> threads;
    std::optional<std::pair<int, int>> source_resolution;
    std::optional<std::pair<int, int>> target_resolution;
    std::optional<ResolutionReference> resolution_reference;
};

constexpr const char* k_profiles_config_filename = "spratprofiles.cfg";
constexpr const char* k_user_profiles_config_relpath = ".config/sprat/spratprofiles.cfg";
constexpr const char* k_global_profiles_config_path = SPRAT_GLOBAL_PROFILE_CONFIG;
constexpr const char k_default_profile_name[] = "fast";
constexpr Mode k_default_mode = Mode::FAST;
constexpr OptimizeTarget k_default_optimize_target = OptimizeTarget::GPU;
constexpr int k_default_padding = 0;
constexpr int k_default_max_combinations = 0;
constexpr double k_default_scale = 1.0;
constexpr bool k_default_trim_transparent = false;
constexpr unsigned int k_default_threads = 0;
constexpr std::array<const char*, 3> k_compact_prewarm_profiles = {
    "desktop",
    "mobile",
    "space",
};

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parse_mode_from_string(const std::string& value, Mode& out, std::string& error) {
    std::string lower = to_lower_copy(value);
    if (lower == "compact") {
        out = Mode::COMPACT;
        return true;
    }
    if (lower == "pot") {
        out = Mode::POT;
        return true;
    }
    if (lower == "fast") {
        out = Mode::FAST;
        return true;
    }
    error = "invalid mode '" + value + "'";
    return false;
}

bool parse_optimize_target_from_string(const std::string& value, OptimizeTarget& out, std::string& error) {
    std::string lower = to_lower_copy(value);
    if (lower == "gpu") {
        out = OptimizeTarget::GPU;
        return true;
    }
    if (lower == "space") {
        out = OptimizeTarget::SPACE;
        return true;
    }
    error = "invalid optimize target '" + value + "'";
    return false;
}

bool parse_resolution_reference_from_string(const std::string& value,
                                            ResolutionReference& out,
                                            std::string& error) {
    std::string lower = to_lower_copy(value);
    if (lower == "largest") {
        out = ResolutionReference::Largest;
        return true;
    }
    if (lower == "smallest") {
        out = ResolutionReference::Smallest;
        return true;
    }
    error = "invalid resolution reference '" + value + "'";
    return false;
}

bool parse_positive_int(const std::string& value, int& out) {
    try {
        size_t idx = 0;
        long long parsed = std::stoll(value, &idx);
        if (idx != value.size() || parsed <= 0 ||
            parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_non_negative_int(const std::string& value, int& out) {
    try {
        size_t idx = 0;
        long long parsed = std::stoll(value, &idx);
        if (idx != value.size() || parsed < 0 ||
            parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_positive_uint(const std::string& value, unsigned int& out) {
    int parsed = 0;
    if (!parse_positive_int(value, parsed)) {
        return false;
    }
    out = static_cast<unsigned int>(parsed);
    return true;
}

bool parse_scale_factor(const std::string& value, double& out) {
    try {
        size_t idx = 0;
        double parsed = std::stod(value, &idx);
        if (idx != value.size() || !std::isfinite(parsed) || parsed <= 0.0 || parsed > 1.0) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_resolution(const std::string& value, int& out_width, int& out_height) {
    if (value.empty()) {
        return false;
    }
    const size_t sep = value.find('x');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= value.size()) {
        return false;
    }
    if (value.find('x', sep + 1) != std::string::npos) {
        return false;
    }

    const std::string width_str = value.substr(0, sep);
    const std::string height_str = value.substr(sep + 1);
    if (!parse_positive_int(width_str, out_width) || !parse_positive_int(height_str, out_height)) {
        return false;
    }
    return true;
}

bool parse_bool_value(const std::string& value, bool& out) {
    std::string lower = to_lower_copy(value);
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parse_profiles_config(std::istream& input,
                           std::vector<ProfileDefinition>& out,
                           std::string& error) {
    out.clear();
    std::unordered_set<std::string> seen_names;
    std::optional<ProfileDefinition> current;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            if (current) {
                out.push_back(*current);
                current.reset();
            }
            std::string header = trimmed.substr(1, trimmed.size() - 2);
            std::istringstream iss(header);
            std::string section_type;
            if (!(iss >> section_type)) {
                error = "empty section header at line " + std::to_string(line_number);
                return false;
            }
            section_type = to_lower_copy(section_type);
            if (section_type != "profile") {
                error = "unsupported section '" + section_type + "' at line " + std::to_string(line_number);
                return false;
            }
            std::string name;
            if (!(iss >> name)) {
                error = "missing profile name at line " + std::to_string(line_number);
                return false;
            }
            std::string extra;
            if (iss >> extra) {
                error = "unexpected token '" + extra + "' in profile header at line " +
                        std::to_string(line_number);
                return false;
            }
            if (seen_names.find(name) != seen_names.end()) {
                error = "duplicate profile '" + name + "' at line " + std::to_string(line_number);
                return false;
            }
            seen_names.insert(name);
            ProfileDefinition def;
            def.name = name;
            def.mode = Mode::COMPACT;
            def.optimize_target = OptimizeTarget::GPU;
            current = def;
            continue;
        }

        if (!current) {
            error = "entry outside of profile section at line " + std::to_string(line_number);
            return false;
        }

        size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            error = "invalid line '" + trimmed + "' at line " + std::to_string(line_number);
            return false;
        }
        std::string key = trim_copy(trimmed.substr(0, equals));
        std::string value = trim_copy(trimmed.substr(equals + 1));
        if (key.empty()) {
            error = "empty key at line " + std::to_string(line_number);
            return false;
        }
        if (value.empty()) {
            error = "empty value for key '" + key + "' at line " + std::to_string(line_number);
            return false;
        }

        std::string lower_key = to_lower_copy(key);
        if (lower_key == "mode") {
            Mode parsed_mode;
            if (!parse_mode_from_string(value, parsed_mode, error)) {
                error += " at line " + std::to_string(line_number);
                return false;
            }
            current->mode = parsed_mode;
        } else if (lower_key == "optimize") {
            OptimizeTarget parsed_target;
            if (!parse_optimize_target_from_string(value, parsed_target, error)) {
                error += " at line " + std::to_string(line_number);
                return false;
            }
            current->optimize_target = parsed_target;
        } else if (lower_key == "max_width" || lower_key == "default_max_width") {
            int parsed_width = 0;
            if (!parse_positive_int(value, parsed_width)) {
                error = "invalid max_width '" + value + "' at line " +
                        std::to_string(line_number);
                return false;
            }
            current->max_width = parsed_width;
        } else if (lower_key == "max_height" || lower_key == "default_max_height") {
            int parsed_height = 0;
            if (!parse_positive_int(value, parsed_height)) {
                error = "invalid max_height '" + value + "' at line " +
                        std::to_string(line_number);
                return false;
            }
            current->max_height = parsed_height;
        } else if (lower_key == "padding") {
            int parsed_padding = 0;
            if (!parse_non_negative_int(value, parsed_padding)) {
                error = "invalid padding '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->padding = parsed_padding;
        } else if (lower_key == "max_combinations") {
            int parsed_max_combinations = 0;
            if (!parse_non_negative_int(value, parsed_max_combinations)) {
                error = "invalid max_combinations '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->max_combinations = parsed_max_combinations;
        } else if (lower_key == "scale") {
            double parsed_scale = 0.0;
            if (!parse_scale_factor(value, parsed_scale)) {
                error = "invalid scale '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->scale = parsed_scale;
        } else if (lower_key == "trim_transparent") {
            bool parsed_trim = false;
            if (!parse_bool_value(value, parsed_trim)) {
                error = "invalid trim_transparent '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->trim_transparent = parsed_trim;
        } else if (lower_key == "threads") {
            unsigned int parsed_threads = 0;
            if (!parse_positive_uint(value, parsed_threads)) {
                error = "invalid threads '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->threads = parsed_threads;
        } else if (lower_key == "source_resolution") {
            int w = 0, h = 0;
            if (!parse_resolution(value, w, h)) {
                error = "invalid source_resolution '" + value + "' at line " + std::to_string(line_number);
                return false;
            }
            current->source_resolution = std::make_pair(w, h);
        } else if (lower_key == "target_resolution") {
            if (to_lower_copy(value) == "source") {
                current->target_resolution = std::make_pair(-1, -1);
            } else {
                int w = 0, h = 0;
                if (!parse_resolution(value, w, h)) {
                    error = "invalid target_resolution '" + value + "' at line " + std::to_string(line_number);
                    return false;
                }
                current->target_resolution = std::make_pair(w, h);
            }
        } else if (lower_key == "resolution_reference") {
            ResolutionReference ref;
            if (!parse_resolution_reference_from_string(value, ref, error)) {
                error += " at line " + std::to_string(line_number);
                return false;
            }
            current->resolution_reference = ref;
        } else {
            error = "unknown key '" + key + "' at line " + std::to_string(line_number);
            return false;
        }
    }

    if (current) {
        out.push_back(*current);
    }

    if (out.empty()) {
        error = "no profiles defined";
        return false;
    }
    return true;
}

bool load_profiles_config_from_file(const fs::path& path,
                                    std::vector<ProfileDefinition>& out,
                                    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "failed to open '" + path.string() + "'";
        return false;
    }
    return parse_profiles_config(input, out, error);
}

std::optional<fs::path> resolve_user_profiles_config_path() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    return fs::path(home) / k_user_profiles_config_relpath;
}

struct Sprite {
    std::string path;
    int w, h;
    int x = 0, y = 0;
    int trim_left = 0, trim_top = 0;
    int trim_right = 0, trim_bottom = 0;
};

struct ImageMeta {
    uintmax_t file_size = 0;
    long long mtime_ticks = 0;
};

struct ImageSource {
    fs::path file_path;
    std::string path;
    ImageMeta meta;
};

struct ImageCacheEntry {
    bool trim_transparent = false;
    uintmax_t file_size = 0;
    long long mtime_ticks = 0;
    int w = 0;
    int h = 0;
    int trim_left = 0;
    int trim_top = 0;
    int trim_right = 0;
    int trim_bottom = 0;
    long long cached_at_unix = 0;
};

struct LayoutCandidate {
    bool valid = false;
    size_t area = 0;
    int w = 0;
    int h = 0;
    std::vector<Sprite> sprites;
};

struct LayoutSeedEntry {
    std::string path;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int trim_left = 0;
    int trim_top = 0;
    int trim_right = 0;
    int trim_bottom = 0;
};

struct LayoutSeedCache {
    std::string signature;
    int padding = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    std::vector<LayoutSeedEntry> entries;
};

struct Node {
    int x, y, w, h;
    bool used = false;
    std::unique_ptr<Node> right;
    std::unique_ptr<Node> down;

    Node(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
};

bool checked_add_int(int a, int b, int& out) {
    if (b > 0 && a > std::numeric_limits<int>::max() - b) {
        return false;
    }
    if (b < 0 && a < std::numeric_limits<int>::min() - b) {
        return false;
    }
    out = a + b;
    return true;
}

bool checked_mul_size_t(size_t a, size_t b, size_t& out) {
    if (a == 0 || b <= std::numeric_limits<size_t>::max() / a) {
        out = a * b;
        return true;
    }
    return false;
}

inline bool pixel_is_opaque(const unsigned char* rgba, int width, int x, int y) {
    if (rgba == nullptr || width <= 0 || x < 0 || y < 0 || x >= width) {
        return false;
    }
    const size_t pixel_index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    const size_t alpha_index = pixel_index * static_cast<size_t>(4) + static_cast<size_t>(3);
    return rgba[alpha_index] != 0;
}

bool compute_trim_bounds(const unsigned char* rgba,
                         int w,
                         int h,
                         int& min_x,
                         int& min_y,
                         int& max_x,
                         int& max_y) {
    min_x = 0;
    min_y = 0;
    max_x = -1;
    max_y = -1;
    if (w <= 0 || h <= 0 || rgba == nullptr) {
        return false;
    }

    // Validate that the image dimensions are reasonable
    if (w > 100000 || h > 100000) {
        return false;
    }

    int top_hit_x = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!pixel_is_opaque(rgba, w, x, y)) {
                continue;
            }
            min_y = y;
            top_hit_x = x;
            break;
        }
        if (top_hit_x >= 0) {
            break;
        }
    }
    if (top_hit_x < 0) {
        return false;
    }

    int bottom_hit_x = -1;
    for (int y = h - 1; y >= min_y; --y) {
        for (int x = w - 1; x >= 0; --x) {
            if (!pixel_is_opaque(rgba, w, x, y)) {
                continue;
            }
            max_y = y;
            bottom_hit_x = x;
            break;
        }
        if (bottom_hit_x >= 0) {
            break;
        }
    }

    const int left_search_end = std::min(top_hit_x, bottom_hit_x);
    min_x = left_search_end;
    for (int x = 0; x <= left_search_end; ++x) {
        bool found = false;
        for (int y = min_y; y <= max_y; ++y) {
            if (!pixel_is_opaque(rgba, w, x, y)) {
                continue;
            }
            min_x = x;
            found = true;
            break;
        }
        if (found) {
            break;
        }
    }

    const int right_search_start = std::max(top_hit_x, bottom_hit_x);
    max_x = right_search_start;
    for (int x = w - 1; x >= right_search_start; --x) {
        bool found = false;
        for (int y = min_y; y <= max_y; ++y) {
            if (!pixel_is_opaque(rgba, w, x, y)) {
                continue;
            }
            max_x = x;
            found = true;
            break;
        }
        if (found) {
            break;
        }
    }

    return max_x >= min_x && max_y >= min_y;
}

bool read_image_meta(const fs::path& path, ImageMeta& out) {
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return false;
    }
    if (size > 1000000000) { // 1GB limit
        return false;
    }
    fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    out.file_size = size;
    out.mtime_ticks = mtime.time_since_epoch().count();
    return true;
}

long long now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool is_supported_image_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty() || ext.size() > 10) {
        return false;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".tga" || ext == ".gif" || ext == ".psd" || ext == ".pic" ||
           ext == ".pnm" || ext == ".pgm" || ext == ".ppm" || ext == ".hdr" ||
           ext == ".webp";
}

enum class ContentType {
    Directory,
    ListFile,
    TarFile,
    CompressedTarFile,
    Unknown
};

ContentType detect_content_type_from_fd(int fd) {
    // Read first 512 bytes to detect content type
    char buffer[512];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read < 257) {
        return ContentType::Unknown;
    }
    
    // Check for tar magic bytes at positions 257-262 (ustar signature)
    if (bytes_read >= 263) {
        const char* ustar_sig = buffer + 257;
        if (strncmp(ustar_sig, "ustar", 5) == 0) {
            return ContentType::TarFile;
        }
    }
    
    // Check for compression magic bytes at the beginning
    if (bytes_read >= 2) {
        // gzip magic: 0x1f 0x8b
        if (static_cast<unsigned char>(buffer[0]) == 0x1f && 
            static_cast<unsigned char>(buffer[1]) == 0x8b) {
            return ContentType::CompressedTarFile;
        }
    }
    
    if (bytes_read >= 3) {
        // bzip2 magic: "BZh"
        if (buffer[0] == 'B' && buffer[1] == 'Z' && buffer[2] == 'h') {
            return ContentType::CompressedTarFile;
        }
    }
    
    if (bytes_read >= 6) {
        // xz magic: 0xfd 0x37 0x7a 0x58 0x5a 0x00
        if (static_cast<unsigned char>(buffer[0]) == 0xfd &&
            static_cast<unsigned char>(buffer[1]) == 0x37 &&
            static_cast<unsigned char>(buffer[2]) == 0x7a &&
            static_cast<unsigned char>(buffer[3]) == 0x58 &&
            static_cast<unsigned char>(buffer[4]) == 0x5a &&
            static_cast<unsigned char>(buffer[5]) == 0x00) {
            return ContentType::CompressedTarFile;
        }
    }
    
    return ContentType::Unknown;
}

ContentType detect_content_type_from_path(const fs::path& path) {
    const bool is_dir = fs::exists(path) && fs::is_directory(path);
    const bool is_file = fs::exists(path) && fs::is_regular_file(path);
    
    if (is_dir) {
        return ContentType::Directory;
    }
    if (!is_file) {
        return ContentType::Unknown;
    }
    
    // For files, check extension first for quick detection
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    if (ext == ".tar") {
        return ContentType::TarFile;
    }
    
    std::string filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    // Check for compressed tar extensions
    if (filename.find(".tar.gz") != std::string::npos ||
        filename.find(".tar.bz2") != std::string::npos ||
        filename.find(".tar.xz") != std::string::npos ||
        filename.find(".tgz") != std::string::npos ||
        filename.find(".tbz2") != std::string::npos ||
        filename.find(".txz") != std::string::npos) {
        return ContentType::CompressedTarFile;
    }
    
    // For other files, assume list file
    return ContentType::ListFile;
}

bool is_tar_file(const fs::path& path) {
    return detect_content_type_from_path(path) == ContentType::TarFile;
}

bool is_compressed_tar_file(const fs::path& path) {
    return detect_content_type_from_path(path) == ContentType::CompressedTarFile;
}

bool extract_tar_file(const fs::path& tar_path, const fs::path& output_dir) {
    struct archive* a = archive_read_new();
    if (!a) {
        std::cerr << "Error: Failed to create archive reader" << std::endl;
        return false;
    }
    
    // Enable all supported formats and compression
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    if (archive_read_open_filename(a, tar_path.string().c_str(), 10240) != ARCHIVE_OK) {
        std::cerr << "Error: Failed to open tar file: " << archive_error_string(a) << std::endl;
        archive_read_free(a);
        return false;
    }
    
    struct archive* ext = archive_write_disk_new();
    if (!ext) {
        std::cerr << "Error: Failed to create archive writer" << std::endl;
        archive_read_free(a);
        return false;
    }
    
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    
    int r;
    struct archive_entry* entry;
    
    while (true) {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to read archive header: " << archive_error_string(a) << std::endl;
            break;
        }
        
        // Skip directories
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }
        
        // Get the filename
        const char* filename = archive_entry_pathname(entry);
        if (!filename) {
            continue;
        }
        
        // Extract to the correct path (preserving directory structure)
        fs::path file_path(filename);
        fs::path output_path = output_dir / file_path;
        
        // Create parent directory if needed
        std::error_code ec;
        fs::create_directories(output_path.parent_path(), ec);
        
        // Set the extraction path
        archive_entry_set_pathname(entry, output_path.string().c_str());
        
        // Extract the file
        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to write archive header: " << archive_error_string(ext) << std::endl;
        } else {
            const void* buff;
            size_t size;
            la_int64_t offset;
            
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                r = archive_write_data_block(ext, buff, size, offset);
                if (r < ARCHIVE_OK) {
                    std::cerr << "Error: Failed to write archive data: " << archive_error_string(ext) << std::endl;
                    break;
                }
            }
            
            if (r < ARCHIVE_OK) {
                std::cerr << "Error: Failed to read archive data: " << archive_error_string(a) << std::endl;
            }
        }
        
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to finish archive entry: " << archive_error_string(ext) << std::endl;
        }
    }
    
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    
    return true;
}

bool extract_tar_from_stdin(const fs::path& output_dir) {
    struct archive* a = archive_read_new();
    if (!a) {
        std::cerr << "Error: Failed to create archive reader" << std::endl;
        return false;
    }
    
    // Enable all supported formats and compression
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    // Open from stdin
    if (archive_read_open_fd(a, STDIN_FILENO, 10240) != ARCHIVE_OK) {
        std::cerr << "Error: Failed to open stdin for tar extraction: " << archive_error_string(a) << std::endl;
        archive_read_free(a);
        return false;
    }
    
    struct archive* ext = archive_write_disk_new();
    if (!ext) {
        std::cerr << "Error: Failed to create archive writer" << std::endl;
        archive_read_free(a);
        return false;
    }
    
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    
    int r;
    struct archive_entry* entry;
    
    while (true) {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to read archive header: " << archive_error_string(a) << std::endl;
            break;
        }
        
        // Skip directories
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }
        
        // Get the filename
        const char* filename = archive_entry_pathname(entry);
        if (!filename) {
            continue;
        }
        
        // Extract to the correct path (preserving directory structure)
        fs::path file_path(filename);
        fs::path output_path = output_dir / file_path;
        
        // Create parent directory if needed
        std::error_code ec;
        fs::create_directories(output_path.parent_path(), ec);
        
        // Set the extraction path
        archive_entry_set_pathname(entry, output_path.string().c_str());
        
        // Extract the file
        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to write archive header: " << archive_error_string(ext) << std::endl;
        } else {
            const void* buff;
            size_t size;
            la_int64_t offset;
            
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                r = archive_write_data_block(ext, buff, size, offset);
                if (r < ARCHIVE_OK) {
                    std::cerr << "Error: Failed to write archive data: " << archive_error_string(ext) << std::endl;
                    break;
                }
            }
            
            if (r < ARCHIVE_OK) {
                std::cerr << "Error: Failed to read archive data: " << archive_error_string(a) << std::endl;
            }
        }
        
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK) {
            std::cerr << "Error: Failed to finish archive entry: " << archive_error_string(ext) << std::endl;
        }
    }
    
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    
    return true;
}

enum class InputType {
    Directory,
    ListFile,
    TarFile,
    StdinTar
};

struct InputContext {
    InputType type;
    fs::path working_folder;
    std::vector<fs::path> temp_dirs_to_cleanup;
};

bool detect_and_extract_tar_content(const fs::path& input_path, InputContext& out_context) {
    const bool is_dir = fs::exists(input_path) && fs::is_directory(input_path);
    const bool is_file = fs::exists(input_path) && fs::is_regular_file(input_path);
    const bool is_tar = is_file && is_tar_file(input_path);
    const bool is_compressed_tar = is_file && is_compressed_tar_file(input_path);
    
    out_context.temp_dirs_to_cleanup.clear();
    
    if (is_tar || is_compressed_tar) {
        // Create a temporary directory for extraction
        fs::path temp_dir = fs::temp_directory_path() / "spratlayout_extract";
        std::error_code ec;
        fs::create_directories(temp_dir, ec);
        if (ec) {
            std::cerr << "Error: Failed to create temporary directory for tar extraction\n";
            return false;
        }
        
        out_context.temp_dirs_to_cleanup.push_back(temp_dir);
        
        // Extract the tar file
        if (!extract_tar_file(input_path, temp_dir)) {
            std::cerr << "Error: Failed to extract tar file: " << input_path << "\n";
            // Cleanup on error
            for (const auto& dir : out_context.temp_dirs_to_cleanup) {
                fs::remove_all(dir, ec);
            }
            return false;
        }
        
        out_context.type = InputType::TarFile;
        out_context.working_folder = temp_dir;
        return true;
    } else if (is_dir) {
        out_context.type = InputType::Directory;
        out_context.working_folder = input_path;
        return true;
    } else if (is_file) {
        out_context.type = InputType::ListFile;
        out_context.working_folder = input_path;
        return true;
    }
    
    return false;
}

bool load_content_from_stdin(InputContext& out_context) {
    // Create a temporary directory for extraction
    fs::path temp_dir = fs::temp_directory_path() / "spratlayout_extract_stdin";
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        std::cerr << "Error: Failed to create temporary directory for stdin tar extraction\n";
        return false;
    }
    
    out_context.temp_dirs_to_cleanup.push_back(temp_dir);
    
    // Extract from stdin
    if (!extract_tar_from_stdin(temp_dir)) {
        std::cerr << "Error: Failed to extract tar from stdin\n";
        // Cleanup on error
        for (const auto& dir : out_context.temp_dirs_to_cleanup) {
            fs::remove_all(dir, ec);
        }
        return false;
    }
    
    out_context.type = InputType::StdinTar;
    out_context.working_folder = temp_dir;
    
    return true;
}

void prune_stale_cache_entries(std::unordered_map<std::string, ImageCacheEntry>& entries,
                               long long now_unix,
                               long long max_age_seconds) {
    if (max_age_seconds < 0 || max_age_seconds > 31536000) { // 1 year limit
        max_age_seconds = 86400; // default to 1 day
    }
    for (auto it = entries.begin(); it != entries.end();) {
        const long long cached_at = it->second.cached_at_unix;
        if (cached_at <= 0 || cached_at > now_unix || (now_unix - cached_at) > max_age_seconds) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

bool load_image_cache(const fs::path& cache_path,
                      std::unordered_map<std::string, ImageCacheEntry>& out) {
    out.clear();
    std::ifstream in(cache_path);
    if (!in) {
        return false;
    }

    std::string header_tag;
    int version = 0;
    if (!(in >> header_tag >> version)) {
        return false;
    }
    if (header_tag != "spratlayout_cache" || (version != 1 && version != 2)) {
        return false;
    }

    std::string path;
    int trim_flag = 0;
    ImageCacheEntry entry;
    while (true) {
        entry = ImageCacheEntry{};
        if (!(in >> std::quoted(path)
                 >> trim_flag
                 >> entry.file_size
                 >> entry.mtime_ticks
                 >> entry.w
                 >> entry.h
                 >> entry.trim_left
                 >> entry.trim_top
                 >> entry.trim_right
                 >> entry.trim_bottom)) {
            break;
        }
        if (version == 2) {
            if (!(in >> entry.cached_at_unix)) {
                break;
            }
        }
        if (entry.w <= 0 || entry.h <= 0 || entry.w > 100000 || entry.h > 100000) {
            continue;
        }
        entry.trim_transparent = trim_flag != 0;
        const std::string key = path + (entry.trim_transparent ? "|1" : "|0");
        out[key] = entry;
    }

    return true;
}

bool save_image_cache(const fs::path& cache_path,
                      const std::unordered_map<std::string, ImageCacheEntry>& entries) {
    if (entries.size() > 10000) { // Limit cache size
        return false;
    }

    fs::path tmp = cache_path;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "spratlayout_cache 2\n";
    for (const auto& kv : entries) {
        std::string path = kv.first;
        if (path.size() > 2 &&
            path[path.size() - 2] == '|' &&
            (path.back() == '0' || path.back() == '1')) {
            path = path.substr(0, path.size() - 2);
        }
        const ImageCacheEntry& e = kv.second;
        if (e.w <= 0 || e.h <= 0 || e.w > 100000 || e.h > 100000) {
            continue;
        }
        out << std::quoted(path) << " "
            << (e.trim_transparent ? 1 : 0) << " "
            << e.file_size << " "
            << e.mtime_ticks << " "
            << e.w << " "
            << e.h << " "
            << e.trim_left << " "
            << e.trim_top << " "
            << e.trim_right << " "
            << e.trim_bottom << " "
            << e.cached_at_unix << "\n";
    }
    out.close();
    if (!out) {
        return false;
    }

    std::error_code ec;
    fs::rename(tmp, cache_path, ec);
    if (ec) {
        fs::remove(cache_path, ec);
        ec.clear();
        fs::rename(tmp, cache_path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

fs::path default_temp_dir() {
    std::error_code ec;
    fs::path path = fs::temp_directory_path(ec);
    if (!ec && !path.empty()) {
        return path;
    }

    const char* tmp = std::getenv("TMP");
    if (tmp != nullptr && *tmp != '\0') {
        return fs::path(tmp);
    }
    const char* temp = std::getenv("TEMP");
    if (temp != nullptr && *temp != '\0') {
        return fs::path(temp);
    }
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir != nullptr && *tmpdir != '\0') {
        return fs::path(tmpdir);
    }

    return fs::path("/tmp");
}

fs::path cache_root_dir() {
    fs::path root = default_temp_dir() / "sprat";
    std::error_code ec;
    fs::create_directories(root, ec);
    if (!ec) {
        return root;
    }
    return default_temp_dir();
}

fs::path build_cache_path(const fs::path& folder) {
    std::error_code ec;
    fs::path normalized = fs::absolute(folder, ec);
    std::string folder_key = (!ec ? normalized.lexically_normal().string() : folder.string());
    size_t hash = std::hash<std::string>{}(folder_key);

    std::ostringstream name;
    name << "spratlayout_" << std::hex << hash << ".cache";
    return cache_root_dir() / name.str();
}

fs::path build_output_cache_path(const fs::path& base_cache_path,
                                 const std::string& layout_signature) {
    return base_cache_path.string() + ".layout." + layout_signature;
}

fs::path build_seed_cache_path(const fs::path& base_cache_path,
                               const std::string& seed_signature) {
    return base_cache_path.string() + ".seed." + seed_signature;
}

std::string to_hex_size_t(size_t value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

bool is_file_older_than_seconds(const fs::path& path, long long max_age_seconds) {
    if (max_age_seconds < 0 || max_age_seconds > 31536000) { // 1 year limit
        return true;
    }
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        return true;
    }
    fs::file_time_type file_time = fs::last_write_time(path, ec);
    if (ec) {
        return true;
    }
    fs::file_time_type now = fs::file_time_type::clock::now();
    if (file_time > now) {
        return false;
    }
    long long age = std::chrono::duration_cast<std::chrono::seconds>(now - file_time).count();
    return age > max_age_seconds;
}

std::string build_layout_signature(const std::string& profile_name,
                                   Mode mode,
                                   OptimizeTarget optimize_target,
                                   int max_width_limit,
                                   int max_height_limit,
                                   int padding,
                                   int max_combinations,
                                   double scale,
                                   bool trim_transparent,
                                   bool preserve_source_order,
                                   const std::vector<ImageSource>& sources) {
    std::vector<std::string> parts;
    parts.reserve(sources.size());
    for (const auto& source : sources) {
        std::ostringstream line;
        line << source.path << "|" << source.meta.file_size << "|" << source.meta.mtime_ticks;
        parts.push_back(line.str());
    }
    if (!preserve_source_order) {
        std::sort(parts.begin(), parts.end());
    }

    std::ostringstream sig;
    sig << profile_name << "|"
        << static_cast<int>(mode) << "|"
        << static_cast<int>(optimize_target) << "|"
        << max_width_limit << "|"
        << max_height_limit << "|"
        << padding << "|"
        << max_combinations << "|"
        << std::setprecision(17) << scale << "|"
        << (trim_transparent ? 1 : 0) << "|"
        << (preserve_source_order ? 1 : 0);
    for (const std::string& part : parts) {
        sig << "\n" << part;
    }
    return to_hex_size_t(std::hash<std::string>{}(sig.str()));
}

std::string build_layout_seed_signature(const std::string& profile_name,
                                        Mode mode,
                                        OptimizeTarget optimize_target,
                                        int max_width_limit,
                                        int max_height_limit,
                                        int max_combinations,
                                        double scale,
                                        bool trim_transparent,
                                        bool preserve_source_order,
                                        const std::vector<ImageSource>& sources) {
    std::vector<std::string> parts;
    parts.reserve(sources.size());
    for (const auto& source : sources) {
        std::ostringstream line;
        line << source.path << "|" << source.meta.file_size << "|" << source.meta.mtime_ticks;
        parts.push_back(line.str());
    }
    if (!preserve_source_order) {
        std::sort(parts.begin(), parts.end());
    }

    std::ostringstream sig;
    sig << profile_name << "|"
        << static_cast<int>(mode) << "|"
        << static_cast<int>(optimize_target) << "|"
        << max_width_limit << "|"
        << max_height_limit << "|"
        << max_combinations << "|"
        << std::setprecision(17) << scale << "|"
        << (trim_transparent ? 1 : 0) << "|"
        << (preserve_source_order ? 1 : 0);
    for (const std::string& part : parts) {
        sig << "\n" << part;
    }
    return to_hex_size_t(std::hash<std::string>{}(sig.str()));
}

bool load_output_cache(const fs::path& cache_path,
                       const std::string& expected_signature,
                       std::string& output) {
    std::ifstream in(cache_path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string expected_header = "spratlayout_output_cache " + std::to_string(k_output_cache_format_version);
    std::string header;
    if (!std::getline(in, header) || header != expected_header) {
        return false;
    }

    std::string signature;
    if (!std::getline(in, signature) || signature != expected_signature) {
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return false;
    }
    output = buffer.str();
    return true;
}

bool save_output_cache(const fs::path& cache_path,
                       const std::string& signature,
                       const std::string& output) {
    fs::path tmp = cache_path;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "spratlayout_output_cache " << k_output_cache_format_version << "\n";
    out << signature << "\n";
    out << output;
    out.close();
    if (!out) {
        return false;
    }

    std::error_code ec;
    fs::rename(tmp, cache_path, ec);
    if (ec) {
        fs::remove(cache_path, ec);
        ec.clear();
        fs::rename(tmp, cache_path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

bool load_layout_seed_cache(const fs::path& cache_path,
                            const std::string& expected_signature,
                            LayoutSeedCache& out) {
    out = LayoutSeedCache{};
    std::ifstream in(cache_path);
    if (!in) {
        return false;
    }

    std::string header_tag;
    int version = 0;
    if (!(in >> header_tag >> version)) {
        return false;
    }
    if (header_tag != "spratlayout_seed_cache" || version != k_seed_cache_format_version) {
        return false;
    }

    if (!(in >> out.signature) || out.signature != expected_signature) {
        return false;
    }

    size_t count = 0;
    if (!(in >> out.padding >> out.atlas_width >> out.atlas_height >> count)) {
        return false;
    }
    if (count == 0 || out.atlas_width <= 0 || out.atlas_height <= 0) {
        return false;
    }

    out.entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        LayoutSeedEntry entry;
        if (!(in >> std::quoted(entry.path)
                 >> entry.x >> entry.y
                 >> entry.w >> entry.h
                 >> entry.trim_left >> entry.trim_top
                 >> entry.trim_right >> entry.trim_bottom)) {
            return false;
        }
        out.entries.push_back(std::move(entry));
    }

    return true;
}

bool save_layout_seed_cache(const fs::path& cache_path,
                            const LayoutSeedCache& seed) {
    if (seed.signature.empty() || seed.entries.empty() ||
        seed.atlas_width <= 0 || seed.atlas_height <= 0) {
        return false;
    }

    fs::path tmp = cache_path;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "spratlayout_seed_cache " << k_seed_cache_format_version << "\n";
    out << seed.signature << "\n";
    out << seed.padding << " "
        << seed.atlas_width << " "
        << seed.atlas_height << " "
        << seed.entries.size() << "\n";
    for (const auto& entry : seed.entries) {
        out << std::quoted(entry.path) << " "
            << entry.x << " "
            << entry.y << " "
            << entry.w << " "
            << entry.h << " "
            << entry.trim_left << " "
            << entry.trim_top << " "
            << entry.trim_right << " "
            << entry.trim_bottom << "\n";
    }
    out.close();
    if (!out) {
        return false;
    }

    std::error_code ec;
    fs::rename(tmp, cache_path, ec);
    if (ec) {
        fs::remove(cache_path, ec);
        ec.clear();
        fs::rename(tmp, cache_path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

void prune_cache_family_group(const fs::path& base_cache_path,
                              const std::string& group_suffix,
                              long long max_age_seconds,
                              size_t max_files_to_keep) {
    if (max_files_to_keep == 0) {
        return;
    }

    const fs::path parent = base_cache_path.parent_path();
    if (parent.empty()) {
        return;
    }

    const std::string prefix = base_cache_path.filename().string() + group_suffix;
    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    const fs::file_time_type now = fs::file_time_type::clock::now();
    struct CacheFileInfo {
        fs::path path;
        fs::file_time_type mtime;
    };
    std::vector<CacheFileInfo> keep_candidates;

    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }

        if (name.size() >= 4 && name.substr(name.size() - 4) == ".tmp") {
            fs::remove(entry.path(), ec);
            ec.clear();
            continue;
        }

        fs::file_time_type mtime = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }

        bool remove_for_age = false;
        if (mtime <= now) {
            const long long age = std::chrono::duration_cast<std::chrono::seconds>(now - mtime).count();
            if (age > max_age_seconds) {
                remove_for_age = true;
            }
        }
        if (remove_for_age) {
            fs::remove(entry.path(), ec);
            ec.clear();
            continue;
        }

        keep_candidates.push_back(CacheFileInfo{entry.path(), mtime});
    }

    if (keep_candidates.size() <= max_files_to_keep) {
        return;
    }

    std::sort(keep_candidates.begin(), keep_candidates.end(), [](const CacheFileInfo& a, const CacheFileInfo& b) {
        return a.mtime > b.mtime;
    });

    for (size_t i = max_files_to_keep; i < keep_candidates.size(); ++i) {
        fs::remove(keep_candidates[i].path, ec);
        ec.clear();
    }
}

void prune_cache_family(const fs::path& base_cache_path,
                        long long max_age_seconds,
                        size_t max_layout_files_to_keep,
                        size_t max_seed_files_to_keep) {
    prune_cache_family_group(base_cache_path, ".layout.", max_age_seconds, max_layout_files_to_keep);
    prune_cache_family_group(base_cache_path, ".seed.", max_age_seconds, max_seed_files_to_keep);
}

void prune_all_spratlayout_cache_families(long long max_age_seconds,
                                          size_t max_layout_files_to_keep,
                                          size_t max_seed_files_to_keep) {
    const fs::path parent = cache_root_dir();
    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    std::unordered_set<std::string> base_paths;
    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("spratlayout_", 0) != 0) {
            continue;
        }

        size_t marker = name.find(".cache.layout.");
        if (marker == std::string::npos) {
            marker = name.find(".cache.seed.");
        }
        if (marker == std::string::npos) {
            continue;
        }

        const std::string base_name = name.substr(0, marker + std::string(".cache").size());
        base_paths.insert((parent / base_name).string());
    }

    for (const std::string& base_path : base_paths) {
        prune_cache_family(base_path, max_age_seconds, max_layout_files_to_keep, max_seed_files_to_keep);
    }
}

void remove_legacy_top_level_cache_files() {
    const fs::path parent = default_temp_dir();
    const fs::path active_root = cache_root_dir();
    if (parent == active_root) {
        return;
    }

    std::error_code ec;
    if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec)) {
        return;
    }

    for (fs::directory_iterator it(parent, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("spratlayout_", 0) != 0) {
            continue;
        }
        if (name.find(".cache") == std::string::npos) {
            continue;
        }
        fs::remove(entry.path(), ec);
        ec.clear();
    }
}

bool try_apply_layout_seed(const LayoutSeedCache& seed,
                           int padding,
                           int width_upper_bound,
                           int height_upper_bound,
                           const std::vector<Sprite>& source_sprites,
                           std::vector<Sprite>& out_sprites,
                           int& out_atlas_width,
                           int& out_atlas_height) {
    if (seed.entries.size() != source_sprites.size()) {
        return false;
    }

    std::unordered_map<std::string, const LayoutSeedEntry*> seed_by_path;
    seed_by_path.reserve(seed.entries.size());
    for (const auto& entry : seed.entries) {
        auto inserted = seed_by_path.emplace(entry.path, &entry);
        if (!inserted.second) {
            return false;
        }
    }

    std::unordered_set<std::string> seen_paths;
    seen_paths.reserve(source_sprites.size());

    struct Rect {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
    };
    std::vector<Rect> rects;
    rects.reserve(source_sprites.size());

    out_sprites.clear();
    out_sprites.reserve(source_sprites.size());
    out_atlas_width = 0;
    out_atlas_height = 0;

    for (const auto& src : source_sprites) {
        if (!seen_paths.emplace(src.path).second) {
            return false;
        }
        auto it = seed_by_path.find(src.path);
        if (it == seed_by_path.end()) {
            return false;
        }
        const LayoutSeedEntry& entry = *it->second;
        if (entry.x < 0 || entry.y < 0 ||
            entry.w != src.w || entry.h != src.h ||
            entry.trim_left != src.trim_left || entry.trim_top != src.trim_top ||
            entry.trim_right != src.trim_right || entry.trim_bottom != src.trim_bottom) {
            return false;
        }

        int padded_w = 0;
        int padded_h = 0;
        int x1 = 0;
        int y1 = 0;
        if (!checked_add_int(src.w, padding, padded_w) ||
            !checked_add_int(src.h, padding, padded_h) ||
            !checked_add_int(entry.x, padded_w, x1) ||
            !checked_add_int(entry.y, padded_h, y1)) {
            return false;
        }
        if (padded_w <= 0 || padded_h <= 0 || x1 > width_upper_bound || y1 > height_upper_bound) {
            return false;
        }

        Sprite placed = src;
        placed.x = entry.x;
        placed.y = entry.y;
        out_sprites.push_back(std::move(placed));
        rects.push_back(Rect{entry.x, entry.y, x1, y1});
        out_atlas_width = std::max(out_atlas_width, x1);
        out_atlas_height = std::max(out_atlas_height, y1);
    }
    if (out_atlas_width <= 0 || out_atlas_height <= 0) {
        return false;
    }

    std::vector<size_t> order(rects.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return rects[a].x0 < rects[b].x0;
    });

    for (size_t i = 0; i < order.size(); ++i) {
        const Rect& a = rects[order[i]];
        for (size_t j = i + 1; j < order.size(); ++j) {
            const Rect& b = rects[order[j]];
            if (b.x0 >= a.x1) {
                break;
            }
            if (a.y0 < b.y1 && b.y0 < a.y1) {
                return false;
            }
        }
    }

    return true;
}

std::string build_layout_output_text(int atlas_width,
                                     int atlas_height,
                                     double scale,
                                     bool trim_transparent,
                                     const std::vector<Sprite>& sprites) {
    std::ostringstream output;
    output << "atlas " << atlas_width << "," << atlas_height << "\n";
    output << "scale " << std::setprecision(8) << scale << "\n";
    for (const auto& s : sprites) {
        std::string path = s.path;
        size_t pos = 0;
        while ((pos = path.find('"', pos)) != std::string::npos) {
            path.insert(pos, "\\");
            pos += 2;
        }
        output << "sprite \"" << path << "\" "
               << s.x << "," << s.y << " "
               << s.w << "," << s.h;
        if (trim_transparent) {
            output << " " << s.trim_left << "," << s.trim_top
                   << " " << s.trim_right << "," << s.trim_bottom;
        }
        output << "\n";
    }
    return output.str();
}

bool scale_dimension(int input, double scale, int& output) {
    if (input <= 0 || scale <= 0.0) {
        return false;
    }
    long double scaled = static_cast<long double>(input) * static_cast<long double>(scale);
    if (scaled > static_cast<long double>(std::numeric_limits<int>::max())) {
        return false;
    }
    int rounded = static_cast<int>(std::lround(scaled));
    if (rounded <= 0) {
        rounded = 1;
    }
    output = rounded;
    return true;
}

Node* insert(Node* node, Sprite& sprite, int w, int h) {
    if (node->used) {
        if (node->right) {
            if (Node* r = insert(node->right.get(), sprite, w, h)) {
                return r;
            }
        }
        if (node->down) {
            return insert(node->down.get(), sprite, w, h);
        }
        return nullptr;
    }
    if (w > node->w || h > node->h) {
        return nullptr;
    }
    if (w == node->w && h == node->h) {
        node->used = true;
        return node;
    }
    node->used = true;
    node->down = std::make_unique<Node>(node->x, node->y + h, node->w, node->h - h);
    node->right = std::make_unique<Node>(node->x + w, node->y, node->w - w, h);
    return node;
}

bool try_pack(std::unique_ptr<Node>& root, std::vector<Sprite>& sprites, int padding = 0) {
    root->used = false;
    root->right.reset();
    root->down.reset();
    for (auto& sprite : sprites) {
        int w = 0;
        int h = 0;
        if (
            !checked_add_int(sprite.w, padding, w)
            || !checked_add_int(sprite.h, padding, h)
        ) {
            return false;
        }
        Node* node = insert(root.get(), sprite, w, h);
        if (!node) {
            return false;
        }
        sprite.x = node->x;
        sprite.y = node->y;
    }
    return true;
}

enum class SortMode {
    Height,
    Width,
    Area,
    MaxSide,
    Perimeter
};

bool sort_sprites_by_mode(std::vector<Sprite>& sprites, SortMode mode) {
    auto cmp_height = [](const Sprite& a, const Sprite& b) {
        if (a.h != b.h) return a.h > b.h;
        return a.w > b.w;
    };
    auto cmp_width = [](const Sprite& a, const Sprite& b) {
        if (a.w != b.w) return a.w > b.w;
        return a.h > b.h;
    };
    auto cmp_area = [](const Sprite& a, const Sprite& b) {
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        if (area_a != area_b) return area_a > area_b;
        if (a.h != b.h) return a.h > b.h;
        return a.w > b.w;
    };
    auto cmp_max_side = [](const Sprite& a, const Sprite& b) {
        int max_a = std::max(a.w, a.h);
        int max_b = std::max(b.w, b.h);
        if (max_a != max_b) return max_a > max_b;
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };
    auto cmp_perimeter = [](const Sprite& a, const Sprite& b) {
        int p_a = a.w + a.h;
        int p_b = b.w + b.h;
        if (p_a != p_b) return p_a > p_b;
        long long area_a = static_cast<long long>(a.w) * static_cast<long long>(a.h);
        long long area_b = static_cast<long long>(b.w) * static_cast<long long>(b.h);
        return area_a > area_b;
    };

    switch (mode) {
        case SortMode::Height:
            std::sort(sprites.begin(), sprites.end(), cmp_height);
            return true;
        case SortMode::Width:
            std::sort(sprites.begin(), sprites.end(), cmp_width);
            return true;
        case SortMode::Area:
            std::sort(sprites.begin(), sprites.end(), cmp_area);
            return true;
        case SortMode::MaxSide:
            std::sort(sprites.begin(), sprites.end(), cmp_max_side);
            return true;
        case SortMode::Perimeter:
            std::sort(sprites.begin(), sprites.end(), cmp_perimeter);
            return true;
    }
    return false;
}

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class RectHeuristic {
    BestShortSideFit,
    BestAreaFit,
    BottomLeft
};

bool rects_intersect(const Rect& a, const Rect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

bool rect_contains(const Rect& a, const Rect& b) {
    return b.x >= a.x && b.y >= a.y &&
           b.x + b.w <= a.x + a.w &&
           b.y + b.h <= a.y + a.h;
}

bool split_free_rect(const Rect& free_rect, const Rect& used_rect, std::vector<Rect>& out) {
    if (!rects_intersect(free_rect, used_rect)) {
        out.push_back(free_rect);
        return true;
    }

    int free_right = free_rect.x + free_rect.w;
    int free_bottom = free_rect.y + free_rect.h;
    int used_right = used_rect.x + used_rect.w;
    int used_bottom = used_rect.y + used_rect.h;

    if (used_rect.x > free_rect.x) {
        out.push_back({free_rect.x, free_rect.y, used_rect.x - free_rect.x, free_rect.h});
    }
    if (used_right < free_right) {
        out.push_back({used_right, free_rect.y, free_right - used_right, free_rect.h});
    }
    if (used_rect.y > free_rect.y) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            out.push_back({x0, free_rect.y, x1 - x0, used_rect.y - free_rect.y});
        }
    }
    if (used_bottom < free_bottom) {
        int x0 = std::max(free_rect.x, used_rect.x);
        int x1 = std::min(free_right, used_right);
        if (x1 > x0) {
            out.push_back({x0, used_bottom, x1 - x0, free_bottom - used_bottom});
        }
    }
    return true;
}

void prune_free_rects(std::vector<Rect>& free_rects) {
    size_t i = 0;
    while (i < free_rects.size()) {
        bool removed_i = false;
        size_t j = i + 1;
        while (j < free_rects.size()) {
            if (rect_contains(free_rects[i], free_rects[j])) {
                free_rects.erase(free_rects.begin() + static_cast<std::ptrdiff_t>(j));
                continue;
            }
            if (rect_contains(free_rects[j], free_rects[i])) {
                free_rects.erase(free_rects.begin() + static_cast<std::ptrdiff_t>(i));
                removed_i = true;
                break;
            }
            ++j;
        }
        if (!removed_i) {
            ++i;
        }
    }
}

bool pack_compact_maxrects(
    std::vector<Sprite>& sprites,
    int width_limit,
    int padding,
    int max_height,
    RectHeuristic heuristic,
    int& out_width,
    int& out_height
) {
    if (width_limit <= 0 || max_height <= 0) {
        return false;
    }

    std::vector<Rect> free_rects;
    free_rects.push_back({0, 0, width_limit, max_height});

    int used_w = 0;
    int used_h = 0;

    for (auto& s : sprites) {
        int rw = 0;
        int rh = 0;
        if (
            !checked_add_int(s.w, padding, rw)
            || !checked_add_int(s.h, padding, rh)
            || rw <= 0 || rh <= 0 || rw > width_limit || rh > max_height
        ) {
            return false;
        }

        int best_index = -1;
        int best_short_fit = std::numeric_limits<int>::max();
        int best_long_fit = std::numeric_limits<int>::max();
        int best_area_fit = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_left = std::numeric_limits<int>::max();

        for (size_t i = 0; i < free_rects.size(); ++i) {
            const Rect& fr = free_rects[i];
            if (rw > fr.w || rh > fr.h) {
                continue;
            }

            int leftover_h = fr.h - rh;
            int leftover_w = fr.w - rw;
            int short_fit = std::min(leftover_h, leftover_w);
            int long_fit = std::max(leftover_h, leftover_w);
            int area_fit = leftover_h * leftover_w;

            bool better = false;
            if (heuristic == RectHeuristic::BestShortSideFit) {
                better = short_fit < best_short_fit ||
                         (short_fit == best_short_fit && long_fit < best_long_fit) ||
                         (short_fit == best_short_fit && long_fit == best_long_fit && fr.y < best_top) ||
                         (short_fit == best_short_fit && long_fit == best_long_fit && fr.y == best_top && fr.x < best_left);
            } else if (heuristic == RectHeuristic::BestAreaFit) {
                better = area_fit < best_area_fit ||
                         (area_fit == best_area_fit && short_fit < best_short_fit) ||
                         (area_fit == best_area_fit && short_fit == best_short_fit && fr.y < best_top) ||
                         (area_fit == best_area_fit && short_fit == best_short_fit && fr.y == best_top && fr.x < best_left);
            } else {
                better = fr.y < best_top || (fr.y == best_top && fr.x < best_left) ||
                         (fr.y == best_top && fr.x == best_left && short_fit < best_short_fit);
            }

            if (better) {
                best_index = static_cast<int>(i);
                best_short_fit = short_fit;
                best_long_fit = long_fit;
                best_area_fit = area_fit;
                best_top = fr.y;
                best_left = fr.x;
            }
        }

        if (best_index < 0) {
            return false;
        }

        Rect used = {free_rects[static_cast<size_t>(best_index)].x,
                     free_rects[static_cast<size_t>(best_index)].y,
                     rw, rh};
        s.x = used.x;
        s.y = used.y;

        if (used.x + used.w > used_w) used_w = used.x + used.w;
        if (used.y + used.h > used_h) used_h = used.y + used.h;

        std::vector<Rect> next_free;
        next_free.reserve(free_rects.size() * 2);
        for (const auto& fr : free_rects) {
            if (!split_free_rect(fr, used, next_free)) {
                return false;
            }
        }

        free_rects.clear();
        free_rects.reserve(next_free.size());
        for (const auto& r : next_free) {
            if (r.w > 0 && r.h > 0) {
                free_rects.push_back(r);
            }
        }
        prune_free_rects(free_rects);
    }

    out_width = used_w;
    out_height = used_h;
    return out_width > 0 && out_height > 0;
}

bool pack_fast_shelf(
    std::vector<Sprite>& sprites,
    int max_row_width,
    int padding,
    int& out_width,
    int& out_height
) {
    int x = 0;
    int y = 0;
    int row_height = 0;
    int atlas_width = 0;
    if (max_row_width <= 0) {
        return false;
    }

    for (auto& s : sprites) {
        int w = 0;
        int h = 0;
        int candidate_x = 0;
        int next_y = 0;

        if (
            !checked_add_int(s.w, padding, w)
            || !checked_add_int(s.h, padding, h)
            || w <= 0 || h <= 0
            || w > max_row_width
            || !checked_add_int(x, w, candidate_x)
        ) {
            return false;
        }
        
        if (x > 0 && candidate_x > max_row_width) {
            if (!checked_add_int(y, row_height, next_y)) {
                return false;
            }
            y = next_y;
            x = 0;
            row_height = 0;
            if (!checked_add_int(x, w, candidate_x)) {
                return false;
            }
        }

        s.x = x;
        s.y = y;
        x = candidate_x;
        if (h > row_height) {
            row_height = h;
        }
        if (x > atlas_width) {
            atlas_width = x;
        }
    }

    int total_height = 0;
    if (!checked_add_int(y, row_height, total_height)) {
        return false;
    }
    out_width = atlas_width;
    out_height = total_height;
    return out_width > 0 && out_height > 0;
}

bool compute_tight_atlas_bounds(const std::vector<Sprite>& sprites, int& out_width, int& out_height) {
    out_width = 0;
    out_height = 0;
    for (const auto& s : sprites) {
        int x1 = 0;
        int y1 = 0;
        if (!checked_add_int(s.x, s.w, x1) || !checked_add_int(s.y, s.h, y1)) {
            return false;
        }
        if (x1 > out_width) {
            out_width = x1;
        }
        if (y1 > out_height) {
            out_height = y1;
        }
    }
    return out_width > 0 && out_height > 0;
}

int next_power_of_two(int v) {
    if (v <= 1) {
        return 1;
    }

    int p = 1;
    while (p < v) {
        if (p > std::numeric_limits<int>::max() / 2) {
            return -1;
        }
        p <<= 1;
    }
    return p;
}

bool pick_better_layout_candidate(
    size_t candidate_area,
    int candidate_w,
    int candidate_h,
    bool have_best,
    size_t best_area,
    int best_w,
    int best_h,
    OptimizeTarget optimize_target
) {
    if (!have_best) {
        return true;
    }

    int candidate_max_side = std::max(candidate_w, candidate_h);
    int best_max_side = std::max(best_w, best_h);
    int candidate_aspect_delta = std::abs(candidate_w - candidate_h);
    int best_aspect_delta = std::abs(best_w - best_h);

    if (optimize_target == OptimizeTarget::GPU) {
        if (candidate_max_side != best_max_side) {
            return candidate_max_side < best_max_side;
        }
        if (candidate_area != best_area) {
            return candidate_area < best_area;
        }
        if (candidate_aspect_delta != best_aspect_delta) {
            return candidate_aspect_delta < best_aspect_delta;
        }
        return candidate_w < best_w;
    }

    if (candidate_area != best_area) {
        return candidate_area < best_area;
    }

    if (candidate_max_side != best_max_side) {
        return candidate_max_side < best_max_side;
    }

    if (candidate_aspect_delta != best_aspect_delta) {
        return candidate_aspect_delta < best_aspect_delta;
    }

    return candidate_w < best_w;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: spratlayout <folder> [--profile NAME] [--profiles-config PATH] "
                  << "[--mode compact|pot|fast] [--optimize gpu|space] [--max-width N] [--max-height N] "
                  << "[--padding N] [--max-combinations N] [--source-resolution WxH] [--target-resolution WxH] "
                  << "[--resolution-reference largest|smallest] "
                  << "[--scale F] [--trim-transparent|--no-trim-transparent] "
                  << "[--threads N]\n";
        return 1;
    }

    fs::path folder = argv[1];
    std::string requested_profile_name;
    std::string profiles_config_path;
    bool has_mode_override = false;
    Mode mode_override = Mode::COMPACT;
    bool has_optimize_override = false;
    OptimizeTarget optimize_override = OptimizeTarget::GPU;
    Mode mode = Mode::COMPACT;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    int max_width_limit = 0;
    int max_height_limit = 0;
    bool has_max_width_limit = false;
    bool has_max_height_limit = false;
    int padding = 0;
    bool has_padding_override = false;
    int max_combinations = 0;
    bool has_max_combinations_override = false;
    int source_resolution_width = 0;
    int source_resolution_height = 0;
    int target_resolution_width = 0;
    int target_resolution_height = 0;
    bool has_source_resolution = false;
    bool has_target_resolution = false;
    ResolutionReference resolution_reference = ResolutionReference::Largest;
    bool has_resolution_reference_override = false;
    double scale = 1.0;
    bool has_scale_override = false;
    bool trim_transparent = false;
    bool has_trim_override = false;
    unsigned int thread_limit = 0;
    bool has_threads_override = false;

    // parse args
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            requested_profile_name = argv[++i];
        } else if (arg == "--profiles-config" && i + 1 < argc) {
            profiles_config_path = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string value = argv[++i];
            std::string error;
            if (!parse_mode_from_string(value, mode_override, error)) {
                std::cerr << "Invalid mode value: " << value << "\n";
                return 1;
            }
            has_mode_override = true;
        } else if (arg == "--optimize" && i + 1 < argc) {
            std::string value = argv[++i];
            std::string error;
            if (!parse_optimize_target_from_string(value, optimize_override, error)) {
                std::cerr << "Invalid optimize value: " << value << "\n";
                return 1;
            }
            has_optimize_override = true;
        } else if (arg == "--max-width" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                max_width_limit = std::stoi(value, &idx);
                if (idx != value.size() || max_width_limit <= 0) {
                    std::cerr << "Invalid max width value: " << value << "\n";
                    return 1;
                }
                has_max_width_limit = true;
            } catch (const std::exception&) {
                std::cerr << "Invalid max width value: " << value << "\n";
                return 1;
            }
        } else if (arg == "--max-height" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                max_height_limit = std::stoi(value, &idx);
                if (idx != value.size() || max_height_limit <= 0) {
                    std::cerr << "Invalid max height value: " << value << "\n";
                    return 1;
                }
                has_max_height_limit = true;
            } catch (const std::exception&) {
                std::cerr << "Invalid max height value: " << value << "\n";
                return 1;
            }
        } else if (arg == "--padding" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                padding = std::stoi(value, &idx);
                if (idx != value.size()) {
                    std::cerr << "Invalid padding value: " << value << "\n";
                    return 1;
                }
            } catch (const std::exception&) {
                std::cerr << "Invalid padding value: " << value << "\n";
                return 1;
            }
            if (padding < 0) {
                padding = 0;
            }
            has_padding_override = true;
        } else if (arg == "--max-combinations" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                max_combinations = std::stoi(value, &idx);
                if (idx != value.size() || max_combinations < 0) {
                    std::cerr << "Invalid max combinations value: " << value << "\n";
                    return 1;
                }
            } catch (const std::exception&) {
                std::cerr << "Invalid max combinations value: " << value << "\n";
                return 1;
            }
            has_max_combinations_override = true;
        } else if (arg == "--source-resolution" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_resolution(value, source_resolution_width, source_resolution_height)) {
                std::cerr << "Invalid source resolution value: " << value << "\n";
                return 1;
            }
            has_source_resolution = true;
        } else if (arg == "--target-resolution" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_resolution(value, target_resolution_width, target_resolution_height)) {
                std::cerr << "Invalid target resolution value: " << value << "\n";
                return 1;
            }
            has_target_resolution = true;
        } else if (arg == "--resolution-reference" && i + 1 < argc) {
            if (has_resolution_reference_override) {
                std::cerr << "Error: --resolution-reference can only be provided once\n";
                return 1;
            }
            std::string value = argv[++i];
            std::string error;
            if (!parse_resolution_reference_from_string(value, resolution_reference, error)) {
                std::cerr << "Invalid resolution reference value: " << value << "\n";
                return 1;
            }
            has_resolution_reference_override = true;
        } else if (arg == "--scale" && i + 1 < argc) {
            std::string value = argv[++i];
            if (!parse_scale_factor(value, scale)) {
                std::cerr << "Invalid scale value: " << value << "\n";
                return 1;
            }
            has_scale_override = true;
        } else if (arg == "--trim-transparent") {
            trim_transparent = true;
            has_trim_override = true;
        } else if (arg == "--no-trim-transparent") {
            trim_transparent = false;
            has_trim_override = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            try {
                size_t idx = 0;
                int parsed = std::stoi(value, &idx);
                if (idx != value.size() || parsed <= 0) {
                    std::cerr << "Invalid thread count: " << value << "\n";
                    return 1;
                }
                thread_limit = static_cast<unsigned int>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid thread count: " << value << "\n";
                return 1;
            }
            has_threads_override = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    std::vector<ProfileDefinition> profile_definitions;
    std::unordered_map<std::string, ProfileDefinition> profile_map;
    std::string selected_profile_name = k_default_profile_name;
    const bool has_requested_profile = !requested_profile_name.empty();
    if (has_requested_profile) {
        selected_profile_name = requested_profile_name;
    }

    if (!has_mode_override) {
        mode = k_default_mode;
    } else {
        mode = mode_override;
    }
    if (!has_optimize_override) {
        optimize_target = k_default_optimize_target;
    } else {
        optimize_target = optimize_override;
    }
    if (!has_padding_override) {
        padding = k_default_padding;
    }
    if (!has_max_combinations_override) {
        max_combinations = k_default_max_combinations;
    }
    if (!has_scale_override) {
        scale = k_default_scale;
    }
    if (!has_trim_override) {
        trim_transparent = k_default_trim_transparent;
    }
    if (!has_threads_override) {
        thread_limit = k_default_threads;
    }

    fs::path cwd = fs::current_path();
    fs::path exec_path(argv[0]);
    if (exec_path.is_relative() && !cwd.empty()) {
        exec_path = cwd / exec_path;
    }
    fs::path exec_dir = exec_path.parent_path();
    if (exec_dir.empty()) {
        exec_dir = cwd;
    }

    if (has_requested_profile) {
        std::string config_error;
        std::vector<fs::path> config_candidates;
        if (!profiles_config_path.empty()) {
            fs::path config_candidate(profiles_config_path);
            if (config_candidate.is_relative() && !cwd.empty()) {
                config_candidate = cwd / config_candidate;
            }
            config_candidates.push_back(std::move(config_candidate));
        } else {
            if (std::optional<fs::path> user_config = resolve_user_profiles_config_path()) {
                config_candidates.push_back(*user_config);
            }
            config_candidates.push_back(exec_dir / k_profiles_config_filename);
            config_candidates.push_back(fs::path(k_global_profiles_config_path));
        }

        bool loaded_profile_file = false;
        std::vector<std::string> tried_candidates;
        for (const fs::path& candidate : config_candidates) {
            std::error_code ec;
            const bool exists = fs::exists(candidate, ec);
            if (ec || !exists) {
                tried_candidates.push_back(candidate.string());
                continue;
            }
            if (!load_profiles_config_from_file(candidate, profile_definitions, config_error)) {
                std::cerr << "Failed to load profile config (" << candidate << "): " << config_error << "\n";
                return 1;
            }
            loaded_profile_file = true;
            break;
        }

        if (!loaded_profile_file) {
            std::cerr << "Failed to load profile config. Tried:";
            for (const std::string& candidate : tried_candidates) {
                std::cerr << " " << candidate;
            }
            std::cerr << "\n";
            return 1;
        }

        for (const auto& def : profile_definitions) {
            profile_map.emplace(def.name, def);
        }

        auto profile_it = profile_map.find(selected_profile_name);
        if (profile_it == profile_map.end()) {
            std::string available;
            for (size_t idx = 0; idx < profile_definitions.size(); ++idx) {
                if (idx > 0) {
                    available += ", ";
                }
                available += profile_definitions[idx].name;
            }
            std::cerr << "Invalid profile '" << selected_profile_name << "'. Available profiles: "
                      << available << "\n";
            return 1;
        }

        const ProfileDefinition& selected_profile = profile_it->second;
        if (!has_mode_override) {
            mode = selected_profile.mode;
        }
        if (!has_optimize_override) {
            optimize_target = selected_profile.optimize_target;
        }
        if (!has_max_width_limit && selected_profile.max_width) {
            max_width_limit = *selected_profile.max_width;
        }
        if (!has_max_height_limit && selected_profile.max_height) {
            max_height_limit = *selected_profile.max_height;
        }
        if (!has_padding_override && selected_profile.padding) {
            padding = *selected_profile.padding;
        }
        if (!has_max_combinations_override && selected_profile.max_combinations) {
            max_combinations = *selected_profile.max_combinations;
        }
        if (!has_scale_override && selected_profile.scale) {
            scale = *selected_profile.scale;
        }
        if (!has_trim_override && selected_profile.trim_transparent) {
            trim_transparent = *selected_profile.trim_transparent;
        }
        if (!has_threads_override && selected_profile.threads) {
            thread_limit = *selected_profile.threads;
        }
        if (!has_source_resolution && selected_profile.source_resolution) {
            source_resolution_width = selected_profile.source_resolution->first;
            source_resolution_height = selected_profile.source_resolution->second;
            has_source_resolution = true;
        }
        if (!has_target_resolution && selected_profile.target_resolution) {
            if (selected_profile.target_resolution->first == -1 &&
                selected_profile.target_resolution->second == -1) {
                if (has_source_resolution) {
                    target_resolution_width = source_resolution_width;
                    target_resolution_height = source_resolution_height;
                    has_target_resolution = true;
                }
            } else {
                target_resolution_width = selected_profile.target_resolution->first;
                target_resolution_height = selected_profile.target_resolution->second;
                has_target_resolution = true;
            }
        }
        if (!has_resolution_reference_override && selected_profile.resolution_reference) {
            resolution_reference = *selected_profile.resolution_reference;
        }
    }

    if (has_source_resolution != has_target_resolution) {
        std::cerr << "Error: --source-resolution and --target-resolution must be provided together\n";
        return 1;
    }
    if (has_source_resolution) {
        const double scale_x =
            static_cast<double>(target_resolution_width) / static_cast<double>(source_resolution_width);
        const double scale_y =
            static_cast<double>(target_resolution_height) / static_cast<double>(source_resolution_height);
        const double resolution_scale =
            (resolution_reference == ResolutionReference::Largest)
                ? std::max(scale_x, scale_y)
                : std::min(scale_x, scale_y);
        scale *= resolution_scale;
    }

    InputContext input_context;
    bool loaded_from_stdin = false;
    
    // Check if we should read from stdin (when folder is "-")
    if (folder == "-") {
        if (!load_content_from_stdin(input_context)) {
            std::cerr << "Error: Failed to load content from stdin\n";
            return 1;
        }
        loaded_from_stdin = true;
    } else {
        if (!detect_and_extract_tar_content(folder, input_context)) {
            std::cerr << "Error: Failed to load content from input\n";
            return 1;
        }
    }

    const fs::path cache_path = build_cache_path(input_context.working_folder);
    constexpr long long k_cache_max_age_seconds = 3600;
    constexpr size_t k_cache_max_layout_files = 16;
    constexpr size_t k_cache_max_seed_files = 8;
    const long long now_unix = now_unix_seconds();
    remove_legacy_top_level_cache_files();
    prune_all_spratlayout_cache_families(k_cache_max_age_seconds, k_cache_max_layout_files, k_cache_max_seed_files);
    prune_cache_family(cache_path, k_cache_max_age_seconds, k_cache_max_layout_files, k_cache_max_seed_files);

    std::vector<ImageSource> sources;
    auto add_source = [&](const fs::path& image_path, bool strict) -> bool {
        if (!is_supported_image_extension(image_path)) {
            if (strict) {
                std::cerr << "Invalid extension in list input: " << image_path << "\n";
                return false;
            }
            return true;
        }
        ImageMeta meta;
        if (!read_image_meta(image_path, meta)) {
            if (strict) {
                std::cerr << "Failed to stat image: " << image_path << "\n";
                return false;
            }
            return true;
        }
        ImageSource source;
        source.file_path = image_path;
        source.path = image_path.string();
        source.meta = meta;
        sources.push_back(std::move(source));
        return true;
    };

    if (input_context.type == InputType::Directory) {
        for (const auto& entry : fs::directory_iterator(input_context.working_folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!add_source(entry.path(), false)) {
                continue;
            }
        }
    } else if (input_context.type == InputType::TarFile) {
        // Tar files are already extracted to working_folder - search recursively
        for (const auto& entry : fs::recursive_directory_iterator(input_context.working_folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!add_source(entry.path(), false)) {
                continue;
            }
        }
    } else if (input_context.type == InputType::StdinTar) {
        // Stdin tar files are already extracted to working_folder - search recursively
        for (const auto& entry : fs::recursive_directory_iterator(input_context.working_folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!add_source(entry.path(), false)) {
                continue;
            }
        }
    } else {
        std::ifstream list_file(input_context.working_folder);
        if (!list_file) {
            std::cerr << "Failed to open list file: " << input_context.working_folder << "\n";
            return 1;
        }
        std::string line;
        size_t line_number = 0;
        while (std::getline(list_file, line)) {
            ++line_number;
            std::string trimmed = trim_copy(line);
            if (trimmed.empty() || trimmed.front() == '#') {
                continue;
            }
            fs::path entry_path(trimmed);
            if (entry_path.is_relative()) {
                entry_path = input_context.working_folder.parent_path() / entry_path;
            }
            if (!fs::exists(entry_path) || !fs::is_regular_file(entry_path)) {
                std::cerr << "Invalid image path at line " << line_number << ": " << trimmed << "\n";
                return 1;
            }
            if (!add_source(entry_path, true)) {
                return 1;
            }
        }
    }

    if (sources.empty()) {
        std::cerr << "Error: no valid images found\n";
        return 1;
    }

    const bool is_file = (input_context.type == InputType::ListFile || input_context.type == InputType::StdinTar);
    const std::string layout_signature = build_layout_signature(
        selected_profile_name, mode, optimize_target, max_width_limit, max_height_limit,
        padding, max_combinations, scale, trim_transparent, is_file, sources);
    const std::string layout_seed_signature = build_layout_seed_signature(
        selected_profile_name, mode, optimize_target, max_width_limit, max_height_limit,
        max_combinations, scale, trim_transparent, is_file, sources);
    const fs::path output_cache_path = build_output_cache_path(cache_path, layout_signature);
    const fs::path seed_cache_path = build_seed_cache_path(cache_path, layout_seed_signature);
    if (!is_file_older_than_seconds(output_cache_path, k_cache_max_age_seconds)) {
        std::string cached_output;
        if (load_output_cache(output_cache_path, layout_signature, cached_output)) {
            std::cout << cached_output;
            return 0;
        }
    }

    std::unordered_map<std::string, ImageCacheEntry> cache_entries;
    load_image_cache(cache_path, cache_entries);
    prune_stale_cache_entries(cache_entries, now_unix, k_cache_max_age_seconds);

    std::vector<Sprite> sprites;
    for (const auto& source : sources) {
        const fs::path& file_path = source.file_path;
        const std::string& path = source.path;
        const ImageMeta& meta = source.meta;

        const std::string cache_key = path + (trim_transparent ? "|1" : "|0");
        auto cache_it = cache_entries.find(cache_key);
        if (cache_it != cache_entries.end()) {
            const ImageCacheEntry& cached = cache_it->second;
            if (cached.trim_transparent == trim_transparent &&
                cached.file_size == meta.file_size &&
                cached.mtime_ticks == meta.mtime_ticks) {
                Sprite s;
                s.path = path;
                s.w = cached.w;
                s.h = cached.h;
                s.trim_left = cached.trim_left;
                s.trim_top = cached.trim_top;
                s.trim_right = cached.trim_right;
                s.trim_bottom = cached.trim_bottom;
                sprites.push_back(std::move(s));
                cache_it->second.cached_at_unix = now_unix;
                continue;
            }
        }

        Sprite loaded_sprite;
        loaded_sprite.path = path;
        if (!trim_transparent) {
            int w, h, channels;
            if (!stbi_info(path.c_str(), &w, &h, &channels)) {
                continue;
            }
            loaded_sprite.w = w;
            loaded_sprite.h = h;
            sprites.push_back(loaded_sprite);
            cache_entries[cache_key] = ImageCacheEntry{
                trim_transparent,
                meta.file_size,
                meta.mtime_ticks,
                loaded_sprite.w,
                loaded_sprite.h,
                loaded_sprite.trim_left,
                loaded_sprite.trim_top,
                loaded_sprite.trim_right,
                loaded_sprite.trim_bottom,
                now_unix
            };
            continue;
        }

        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data) {
            continue;
        }

        int min_x = 0;
        int min_y = 0;
        int max_x = -1;
        int max_y = -1;
        if (compute_trim_bounds(data, w, h, min_x, min_y, max_x, max_y)) {
            loaded_sprite.trim_left = min_x;
            loaded_sprite.trim_top = min_y;
            loaded_sprite.trim_right = (w - 1) - max_x;
            loaded_sprite.trim_bottom = (h - 1) - max_y;
            loaded_sprite.w = max_x - min_x + 1;
            loaded_sprite.h = max_y - min_y + 1;
        } else {
            // Fully transparent image: keep a 1x1 transparent region.
            loaded_sprite.trim_left = 0;
            loaded_sprite.trim_top = 0;
            loaded_sprite.trim_right = std::max(0, w - 1);
            loaded_sprite.trim_bottom = std::max(0, h - 1);
            loaded_sprite.w = 1;
            loaded_sprite.h = 1;
        }

        stbi_image_free(data);
        sprites.push_back(loaded_sprite);
        cache_entries[cache_key] = ImageCacheEntry{
            trim_transparent,
            meta.file_size,
            meta.mtime_ticks,
            loaded_sprite.w,
            loaded_sprite.h,
            loaded_sprite.trim_left,
            loaded_sprite.trim_top,
            loaded_sprite.trim_right,
            loaded_sprite.trim_bottom,
            now_unix
        };
    }

    save_image_cache(cache_path, cache_entries);

    if (sprites.empty()) {
        std::cerr << "Error: no valid images found\n";
        return 1;
    }

    if (scale != 1.0) {
        for (auto& s : sprites) {
            int scaled_w = 0;
            int scaled_h = 0;
            if (!scale_dimension(s.w, scale, scaled_w) || !scale_dimension(s.h, scale, scaled_h)) {
                std::cerr << "Error: scaled sprite dimensions are invalid\n";
                return 1;
            }
            s.w = scaled_w;
            s.h = scaled_h;
        }
    }

    int max_width = 0;
    int max_height = 0;
    int sum_width = 0;
    int sum_height = 0;
    size_t total_area = 0;
    for (auto& s : sprites) {
        int padded_w = 0;
        int padded_h = 0;
        if (!checked_add_int(s.w, padding, padded_w)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }
        if (!checked_add_int(s.h, padding, padded_h)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }

        size_t sprite_area = 0;
        if (!checked_mul_size_t(static_cast<size_t>(padded_w), static_cast<size_t>(padded_h), sprite_area) ||
            sprite_area > std::numeric_limits<size_t>::max() - total_area) {
            std::cerr << "Error: total area is too large\n";
            return 1;
        }
        total_area += sprite_area;

        max_width = std::max(max_width, padded_w);
        max_height = std::max(max_height, padded_h);
        if (!checked_add_int(sum_width, padded_w, sum_width) ||
            !checked_add_int(sum_height, padded_h, sum_height)) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }
    }

    int atlas_width = 0, atlas_height = 0;
    int width_upper_bound = sum_width;
    int height_upper_bound = sum_height;
    if (max_width_limit > 0) {
        width_upper_bound = std::min(width_upper_bound, max_width_limit);
    }
    if (max_height_limit > 0) {
        height_upper_bound = std::min(height_upper_bound, max_height_limit);
    }
    if (max_width > width_upper_bound || max_height > height_upper_bound) {
        std::cerr << "Error: sprite dimensions exceed provided atlas limits\n";
        return 1;
    }

    bool reused_layout_seed = false;
    bool have_layout_seed = false;
    LayoutSeedCache seed_cache;
    std::vector<Sprite> seeded_sprites;
    if (!is_file_older_than_seconds(seed_cache_path, k_cache_max_age_seconds) &&
        load_layout_seed_cache(seed_cache_path, layout_seed_signature, seed_cache)) {
        if (seed_cache.padding == padding) {
            have_layout_seed = true;
            if (try_apply_layout_seed(seed_cache, padding, width_upper_bound, height_upper_bound,
                                      sprites, seeded_sprites, atlas_width, atlas_height)) {
                sprites = std::move(seeded_sprites);
                reused_layout_seed = true;
            }
        }
    }

    if (!reused_layout_seed) {
        std::unique_ptr<Node> root;
        const std::array<SortMode, 5> sort_modes = {
            SortMode::Area,
            SortMode::MaxSide,
            SortMode::Height,
            SortMode::Width,
            SortMode::Perimeter
        };
        const std::array<RectHeuristic, 3> rect_heuristics = {
            RectHeuristic::BestShortSideFit,
            RectHeuristic::BestAreaFit,
            RectHeuristic::BottomLeft
        };

    if (mode == Mode::POT) {
        int min_pot_width = next_power_of_two(max_width);
        int min_pot_height = next_power_of_two(max_height);
        if (min_pot_width <= 0 || min_pot_height <= 0) {
            std::cerr << "Error: dimensions are too large\n";
            return 1;
        }

        // First, find an upper bound that can pack, then search all POT
        // rectangles up to that area and pick the least wasteful successful fit.
        int side = std::max(min_pot_width, min_pot_height);
        std::vector<Sprite> best_sprites = sprites;
        int best_w = 0;
        int best_h = 0;
        size_t best_area = 0;
        size_t max_candidate_area = 0;
        bool have_best = false;

        while (true) {
            if (max_width_limit > 0 && side > max_width_limit) {
                std::cerr << "Error: no POT layout fits within max width\n";
                return 1;
            }
            if (max_height_limit > 0 && side > max_height_limit) {
                std::cerr << "Error: no POT layout fits within max height\n";
                return 1;
            }
            for (SortMode sort_mode : sort_modes) {
                std::vector<Sprite> trial_sprites = sprites;
                sort_sprites_by_mode(trial_sprites, sort_mode);
                root = std::make_unique<Node>(0, 0, side, side);
                if (!try_pack(root, trial_sprites, padding)) {
                    continue;
                }
                size_t area = static_cast<size_t>(side) * static_cast<size_t>(side);
                best_sprites = std::move(trial_sprites);
                best_w = side;
                best_h = side;
                best_area = area;
                max_candidate_area = area;
                have_best = true;
                break;
            }
            if (have_best) {
                break;
            }
            if (side > std::numeric_limits<int>::max() / 2) {
                std::cerr << "Error: atlas dimensions overflow\n";
                return 1;
            }
            side *= 2;
        }

        std::vector<int> pot_widths;
        std::vector<int> pot_heights;
        for (int w = min_pot_width; w > 0 && static_cast<size_t>(w) <= best_area; w *= 2) {
            pot_widths.push_back(w);
            if (w > std::numeric_limits<int>::max() / 2) {
                break;
            }
        }
        for (int h = min_pot_height; h > 0 && static_cast<size_t>(h) <= best_area; h *= 2) {
            pot_heights.push_back(h);
            if (h > std::numeric_limits<int>::max() / 2) {
                break;
            }
        }

        for (int w : pot_widths) {
            for (int h : pot_heights) {
                size_t area = static_cast<size_t>(w) * static_cast<size_t>(h);
                if (area > max_candidate_area) {
                    continue;
                }
                if (max_width_limit > 0 && w > max_width_limit) {
                    continue;
                }
                if (max_height_limit > 0 && h > max_height_limit) {
                    continue;
                }
                if (!pick_better_layout_candidate(area, w, h, have_best, best_area, best_w, best_h, optimize_target)) {
                    continue;
                }

                for (SortMode sort_mode : sort_modes) {
                    std::vector<Sprite> trial_sprites = sprites;
                    sort_sprites_by_mode(trial_sprites, sort_mode);
                    root = std::make_unique<Node>(0, 0, w, h);
                    if (!try_pack(root, trial_sprites, padding)) {
                        continue;
                    }

                    best_sprites = std::move(trial_sprites);
                    best_w = w;
                    best_h = h;
                    best_area = area;
                    have_best = true;
                    break;
                }
            }
        }

        if (!have_best) {
            std::cerr << "Error: failed to compute pot layout\n";
            return 1;
        }

        sprites = std::move(best_sprites);
        atlas_width = best_w;
        atlas_height = best_h;
    } else if (mode == Mode::COMPACT) {
        if (sum_width <= 0 || sum_height <= 0) {
            std::cerr << "Error: compact bounds are invalid\n";
            return 1;
        }
        const size_t combination_budget = max_combinations > 0
            ? static_cast<size_t>(max_combinations)
            : std::numeric_limits<size_t>::max();
        std::atomic<size_t> combinations_tested{0};
        auto consume_combination_budget = [&]() -> bool {
            if (combination_budget == std::numeric_limits<size_t>::max()) {
                combinations_tested.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            const size_t previous = combinations_tested.fetch_add(1, std::memory_order_relaxed);
            return previous < combination_budget;
        };
        unsigned int worker_count = thread_limit > 0 ? thread_limit : std::thread::hardware_concurrency();
        if (worker_count == 0) {
            worker_count = 1;
        }

        std::array<std::vector<Sprite>, 5> sorted_sprites_by_mode;
        for (size_t i = 0; i < sort_modes.size(); ++i) {
            sorted_sprites_by_mode[i] = sprites;
            sort_sprites_by_mode(sorted_sprites_by_mode[i], sort_modes[i]);
        }

        int seed_width = max_width;
        if (total_area > 0) {
            long double area_root = std::sqrt(static_cast<long double>(total_area));
            if (area_root > static_cast<long double>(std::numeric_limits<int>::max())) {
                std::cerr << "Error: compact width is too large\n";
                return 1;
            }
            int root_width = static_cast<int>(std::ceil(area_root));
            if (root_width > seed_width) {
                seed_width = root_width;
            }
        }
        if (seed_width > width_upper_bound) {
            seed_width = width_upper_bound;
        }
        if (seed_width < max_width) {
            seed_width = max_width;
        }
        if (have_layout_seed) {
            int seed_hint_width = seed_cache.atlas_width;
            if (padding > seed_cache.padding) {
                int delta = 0;
                if (checked_add_int(seed_hint_width, padding - seed_cache.padding, delta)) {
                    seed_hint_width = delta;
                }
            }
            if (seed_hint_width >= max_width && seed_hint_width <= width_upper_bound) {
                seed_width = seed_hint_width;
            }
        }

        LayoutCandidate best_gpu_candidate;
        LayoutCandidate best_space_candidate;
        auto consider_candidate = [&](LayoutCandidate&& candidate) {
            if (!candidate.valid || candidate.w <= 0 || candidate.h <= 0) {
                return;
            }
            const bool better_gpu =
                !best_gpu_candidate.valid ||
                pick_better_layout_candidate(
                    candidate.area, candidate.w, candidate.h, true,
                    best_gpu_candidate.area, best_gpu_candidate.w, best_gpu_candidate.h,
                    OptimizeTarget::GPU);
            const bool better_space =
                !best_space_candidate.valid ||
                pick_better_layout_candidate(
                    candidate.area, candidate.w, candidate.h, true,
                    best_space_candidate.area, best_space_candidate.w, best_space_candidate.h,
                    OptimizeTarget::SPACE);

            if (!better_gpu && !better_space) {
                return;
            }

            if (better_gpu && better_space) {
                best_gpu_candidate = candidate;
                best_space_candidate = std::move(candidate);
                return;
            }
            if (better_gpu) {
                best_gpu_candidate = std::move(candidate);
                return;
            }
            best_space_candidate = std::move(candidate);
        };

        bool budget_exhausted = false;
        for (size_t sort_idx = 0; sort_idx < sort_modes.size() && !budget_exhausted; ++sort_idx) {
            for (RectHeuristic rect_heuristic : rect_heuristics) {
                if (!consume_combination_budget()) {
                    budget_exhausted = true;
                    break;
                }
                std::vector<Sprite> seed_sprites = sorted_sprites_by_mode[sort_idx];
                int seed_used_w = 0;
                int seed_used_h = 0;
                if (!pack_compact_maxrects(seed_sprites, seed_width, padding, height_upper_bound, rect_heuristic, seed_used_w, seed_used_h)) {
                    continue;
                }
                size_t seed_area = static_cast<size_t>(seed_used_w) * static_cast<size_t>(seed_used_h);
                LayoutCandidate seed_candidate;
                seed_candidate.valid = true;
                seed_candidate.area = seed_area;
                seed_candidate.w = seed_used_w;
                seed_candidate.h = seed_used_h;
                seed_candidate.sprites = std::move(seed_sprites);
                consider_candidate(std::move(seed_candidate));
            }
        }

        if (!best_gpu_candidate.valid && !best_space_candidate.valid) {
            std::cerr << "Error: failed to compute compact layout\n";
            return 1;
        }

        // Guided compact search (no brute-force width scan):
        // start from fast/seed anchors, then probe a small nearby window.
        int fast_target_width = max_width;
        if (total_area > 0) {
            long double area_root = std::sqrt(static_cast<long double>(total_area));
            if (area_root <= static_cast<long double>(std::numeric_limits<int>::max())) {
                int candidate = static_cast<int>(std::ceil(area_root));
                if (candidate > fast_target_width) {
                    fast_target_width = candidate;
                }
            }
        }
        if (fast_target_width > width_upper_bound) {
            fast_target_width = width_upper_bound;
        }
        if (fast_target_width < max_width) {
            fast_target_width = max_width;
        }

        std::unordered_set<int> seen_widths;
        std::vector<int> width_candidates;
        auto add_width_candidate = [&](int width) {
            if (width < max_width || width > width_upper_bound) {
                return;
            }
            if (seen_widths.insert(width).second) {
                width_candidates.push_back(width);
            }
        };

        add_width_candidate(seed_width);
        add_width_candidate(fast_target_width);
        if (have_layout_seed) {
            int seed_hint_width = seed_cache.atlas_width;
            if (padding > seed_cache.padding) {
                int expanded = 0;
                if (checked_add_int(seed_hint_width, padding - seed_cache.padding, expanded)) {
                    seed_hint_width = expanded;
                }
            }
            add_width_candidate(seed_hint_width);
        }

        const int range = std::max(1, width_upper_bound - max_width);
        const int step = std::max(8, range / 24);
        const std::array<int, 11> offsets = {
            0, -1, 1, -2, 2, -4, 4, -8, 8, -12, 12
        };
        const std::array<int, 3> anchor_widths = {seed_width, fast_target_width, max_width};
        for (int anchor : anchor_widths) {
            for (int mul : offsets) {
                const long long width_ll =
                    static_cast<long long>(anchor) +
                    static_cast<long long>(mul) * static_cast<long long>(step);
                if (width_ll < static_cast<long long>(std::numeric_limits<int>::min()) ||
                    width_ll > static_cast<long long>(std::numeric_limits<int>::max())) {
                    continue;
                }
                add_width_candidate(static_cast<int>(width_ll));
            }
        }
        std::sort(width_candidates.begin(), width_candidates.end());

        const std::array<size_t, 3> guided_sort_indices = {2, 0, 1}; // Height, Area, MaxSide
        const std::array<RectHeuristic, 2> guided_heuristics = {
            RectHeuristic::BestShortSideFit,
            RectHeuristic::BestAreaFit
        };

        if (!budget_exhausted && !width_candidates.empty()) {
            worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(width_candidates.size()));
            std::vector<LayoutCandidate> worker_gpu(worker_count);
            std::vector<LayoutCandidate> worker_space(worker_count);
            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (unsigned int worker_index = 0; worker_index < worker_count; ++worker_index) {
                workers.emplace_back([&, worker_index]() {
                    const size_t begin = (width_candidates.size() * worker_index) / worker_count;
                    const size_t end = (width_candidates.size() * (worker_index + 1)) / worker_count;

                    LayoutCandidate local_best_gpu;
                    LayoutCandidate local_best_space;
                    auto consider_local = [&](LayoutCandidate&& candidate) {
                        if (!candidate.valid || candidate.w <= 0 || candidate.h <= 0) {
                            return;
                        }
                        const bool better_gpu =
                            !local_best_gpu.valid ||
                            pick_better_layout_candidate(
                                candidate.area, candidate.w, candidate.h, true,
                                local_best_gpu.area, local_best_gpu.w, local_best_gpu.h,
                                OptimizeTarget::GPU);
                        const bool better_space =
                            !local_best_space.valid ||
                            pick_better_layout_candidate(
                                candidate.area, candidate.w, candidate.h, true,
                                local_best_space.area, local_best_space.w, local_best_space.h,
                                OptimizeTarget::SPACE);
                        if (!better_gpu && !better_space) {
                            return;
                        }
                        if (better_gpu && better_space) {
                            local_best_gpu = candidate;
                            local_best_space = std::move(candidate);
                            return;
                        }
                        if (better_gpu) {
                            local_best_gpu = std::move(candidate);
                            return;
                        }
                        local_best_space = std::move(candidate);
                    };

                    bool local_budget_exhausted = false;
                    for (size_t width_index = begin; width_index < end && !local_budget_exhausted; ++width_index) {
                        const int width = width_candidates[width_index];
                        for (size_t sort_idx : guided_sort_indices) {
                            if (local_budget_exhausted) {
                                break;
                            }
                            for (RectHeuristic rect_heuristic : guided_heuristics) {
                                if (!consume_combination_budget()) {
                                    local_budget_exhausted = true;
                                    break;
                                }
                                std::vector<Sprite> trial_sprites = sorted_sprites_by_mode[sort_idx];
                                int used_w = 0;
                                int used_h = 0;
                                if (!pack_compact_maxrects(trial_sprites, width, padding, height_upper_bound, rect_heuristic, used_w, used_h)) {
                                    continue;
                                }
                                size_t area = static_cast<size_t>(used_w) * static_cast<size_t>(used_h);
                                LayoutCandidate candidate;
                                candidate.valid = true;
                                candidate.area = area;
                                candidate.w = used_w;
                                candidate.h = used_h;
                                candidate.sprites = std::move(trial_sprites);
                                consider_local(std::move(candidate));
                            }
                        }
                    }

                    worker_gpu[worker_index] = std::move(local_best_gpu);
                    worker_space[worker_index] = std::move(local_best_space);
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            for (unsigned int i = 0; i < worker_count; ++i) {
                if (worker_gpu[i].valid) {
                    consider_candidate(std::move(worker_gpu[i]));
                }
                if (worker_space[i].valid) {
                    consider_candidate(std::move(worker_space[i]));
                }
            }

            budget_exhausted = (combination_budget != std::numeric_limits<size_t>::max()) &&
                               (combinations_tested.load(std::memory_order_relaxed) >= combination_budget);
        }

        // Include shelf candidates from same guided widths as a cheap fallback.
        if (!budget_exhausted && !width_candidates.empty()) {
            worker_count = std::min<unsigned int>(worker_count, static_cast<unsigned int>(width_candidates.size()));
            std::vector<LayoutCandidate> worker_gpu(worker_count);
            std::vector<LayoutCandidate> worker_space(worker_count);
            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (unsigned int worker_index = 0; worker_index < worker_count; ++worker_index) {
                workers.emplace_back([&, worker_index]() {
                    const size_t begin = (width_candidates.size() * worker_index) / worker_count;
                    const size_t end = (width_candidates.size() * (worker_index + 1)) / worker_count;

                    LayoutCandidate local_best_gpu;
                    LayoutCandidate local_best_space;
                    auto consider_local = [&](LayoutCandidate&& candidate) {
                        if (!candidate.valid || candidate.w <= 0 || candidate.h <= 0) {
                            return;
                        }
                        const bool better_gpu =
                            !local_best_gpu.valid ||
                            pick_better_layout_candidate(
                                candidate.area, candidate.w, candidate.h, true,
                                local_best_gpu.area, local_best_gpu.w, local_best_gpu.h,
                                OptimizeTarget::GPU);
                        const bool better_space =
                            !local_best_space.valid ||
                            pick_better_layout_candidate(
                                candidate.area, candidate.w, candidate.h, true,
                                local_best_space.area, local_best_space.w, local_best_space.h,
                                OptimizeTarget::SPACE);
                        if (!better_gpu && !better_space) {
                            return;
                        }
                        if (better_gpu && better_space) {
                            local_best_gpu = candidate;
                            local_best_space = std::move(candidate);
                            return;
                        }
                        if (better_gpu) {
                            local_best_gpu = std::move(candidate);
                            return;
                        }
                        local_best_space = std::move(candidate);
                    };

                    bool local_budget_exhausted = false;
                    for (size_t width_index = begin; width_index < end && !local_budget_exhausted; ++width_index) {
                        const int width = width_candidates[width_index];
                        for (size_t sort_idx : guided_sort_indices) {
                            if (!consume_combination_budget()) {
                                local_budget_exhausted = true;
                                break;
                            }
                            std::vector<Sprite> shelf_sprites = sorted_sprites_by_mode[sort_idx];
                            int shelf_w = 0;
                            int shelf_h = 0;
                            if (!pack_fast_shelf(shelf_sprites, width, padding, shelf_w, shelf_h)) {
                                continue;
                            }
                            if (shelf_h > height_upper_bound) {
                                continue;
                            }
                            size_t shelf_area = static_cast<size_t>(shelf_w) * static_cast<size_t>(shelf_h);
                            LayoutCandidate shelf_candidate;
                            shelf_candidate.valid = true;
                            shelf_candidate.area = shelf_area;
                            shelf_candidate.w = shelf_w;
                            shelf_candidate.h = shelf_h;
                            shelf_candidate.sprites = std::move(shelf_sprites);
                            consider_local(std::move(shelf_candidate));
                        }
                    }

                    worker_gpu[worker_index] = std::move(local_best_gpu);
                    worker_space[worker_index] = std::move(local_best_space);
                });
            }
            for (auto& worker : workers) {
                worker.join();
            }
            for (unsigned int i = 0; i < worker_count; ++i) {
                if (worker_gpu[i].valid) {
                    consider_candidate(std::move(worker_gpu[i]));
                }
                if (worker_space[i].valid) {
                    consider_candidate(std::move(worker_space[i]));
                }
            }
        }

        const LayoutCandidate* selected_candidate = nullptr;
        if (optimize_target == OptimizeTarget::GPU) {
            selected_candidate = best_gpu_candidate.valid ? &best_gpu_candidate : &best_space_candidate;
        } else {
            selected_candidate = best_space_candidate.valid ? &best_space_candidate : &best_gpu_candidate;
        }
        if (!selected_candidate || !selected_candidate->valid) {
            std::cerr << "Error: failed to compute compact layout\n";
            return 1;
        }

        sprites = selected_candidate->sprites;
        atlas_width = selected_candidate->w;
        atlas_height = selected_candidate->h;

        if (best_gpu_candidate.valid && best_space_candidate.valid) {
            for (const char* profile_name : k_compact_prewarm_profiles) {
                auto prewarm_it = profile_map.find(profile_name);
                if (prewarm_it == profile_map.end()) {
                    continue;
                }
                const ProfileDefinition& compact_profile = prewarm_it->second;
                const Mode prewarm_mode =
                    has_mode_override ? mode_override : compact_profile.mode;
                const OptimizeTarget prewarm_optimize_target =
                    has_optimize_override ? optimize_override : compact_profile.optimize_target;
                if (prewarm_mode != Mode::COMPACT) {
                    continue;
                }
                const int prewarm_max_width =
                    has_max_width_limit
                        ? max_width_limit
                        : (compact_profile.max_width ? *compact_profile.max_width : 0);
                const int prewarm_max_height =
                    has_max_height_limit
                        ? max_height_limit
                        : (compact_profile.max_height ? *compact_profile.max_height : 0);
                const int prewarm_padding =
                    has_padding_override
                        ? padding
                        : (compact_profile.padding ? *compact_profile.padding : 0);
                const int prewarm_max_combinations =
                    has_max_combinations_override
                        ? max_combinations
                        : (compact_profile.max_combinations ? *compact_profile.max_combinations : 0);
                const double prewarm_scale =
                    has_scale_override
                        ? scale
                        : (compact_profile.scale ? *compact_profile.scale : 1.0);
                const bool prewarm_trim_transparent =
                    has_trim_override
                        ? trim_transparent
                        : (compact_profile.trim_transparent ? *compact_profile.trim_transparent : false);
                const std::string prewarm_signature = build_layout_signature(
                    compact_profile.name,
                    prewarm_mode,
                    prewarm_optimize_target,
                    prewarm_max_width,
                    prewarm_max_height,
                    prewarm_padding,
                    prewarm_max_combinations,
                    prewarm_scale,
                    prewarm_trim_transparent,
                    is_file,
                    sources
                );
                if (prewarm_signature == layout_signature) {
                    continue;
                }

                const LayoutCandidate& prewarm_candidate =
                    prewarm_optimize_target == OptimizeTarget::GPU
                        ? best_gpu_candidate
                        : best_space_candidate;
                const std::string prewarm_output = build_layout_output_text(
                    prewarm_candidate.w,
                    prewarm_candidate.h,
                    prewarm_scale,
                    prewarm_trim_transparent,
                    prewarm_candidate.sprites
                );
                save_output_cache(
                    build_output_cache_path(cache_path, prewarm_signature),
                    prewarm_signature,
                    prewarm_output
                );
            }
        }
    } else {
        int target_width = max_width;
        if (total_area > 0) {
            long double area_root = std::sqrt(static_cast<long double>(total_area));
            if (area_root > static_cast<long double>(std::numeric_limits<int>::max())) {
                std::cerr << "Error: fast width is too large\n";
                return 1;
            }
            int candidate = static_cast<int>(std::ceil(area_root));
            if (candidate > target_width) {
                target_width = candidate;
            }
        }
        if (target_width > width_upper_bound) {
            target_width = width_upper_bound;
        }
        if (have_layout_seed) {
            int seed_hint_width = seed_cache.atlas_width;
            if (padding > seed_cache.padding) {
                int expanded = 0;
                if (checked_add_int(seed_hint_width, padding - seed_cache.padding, expanded)) {
                    seed_hint_width = expanded;
                }
            }
            if (seed_hint_width > target_width && seed_hint_width <= width_upper_bound) {
                target_width = seed_hint_width;
            }
        }

        std::vector<Sprite> sorted_sprites = sprites;
        sort_sprites_by_mode(sorted_sprites, SortMode::Height);

        bool packed = false;
        for (int width = target_width; width <= width_upper_bound; ++width) {
            std::vector<Sprite> trial_sprites = sorted_sprites;
            int packed_width = 0;
            int packed_height = 0;
            if (!pack_fast_shelf(trial_sprites, width, padding, packed_width, packed_height)) {
                continue;
            }
            if (packed_height > height_upper_bound) {
                continue;
            }
            sprites = std::move(trial_sprites);
            atlas_width = packed_width;
            atlas_height = packed_height;
            packed = true;
            break;
        }
        if (!packed) {
            std::cerr << "Error: failed to compute fast layout\n";
            return 1;
        }
    }
    }

    if (padding > 0) {
        if (!compute_tight_atlas_bounds(sprites, atlas_width, atlas_height)) {
            std::cerr << "Error: failed to compute final atlas bounds\n";
            return 1;
        }
    }

    LayoutSeedCache next_seed;
    next_seed.signature = layout_seed_signature;
    next_seed.padding = padding;
    next_seed.atlas_width = atlas_width;
    next_seed.atlas_height = atlas_height;
    next_seed.entries.reserve(sprites.size());
    for (const auto& s : sprites) {
        LayoutSeedEntry entry;
        entry.path = s.path;
        entry.x = s.x;
        entry.y = s.y;
        entry.w = s.w;
        entry.h = s.h;
        entry.trim_left = s.trim_left;
        entry.trim_top = s.trim_top;
        entry.trim_right = s.trim_right;
        entry.trim_bottom = s.trim_bottom;
        next_seed.entries.push_back(std::move(entry));
    }
    save_layout_seed_cache(seed_cache_path, next_seed);

    const std::string output_text = build_layout_output_text(
        atlas_width,
        atlas_height,
        scale,
        trim_transparent,
        sprites
    );
    std::cout << output_text;
    save_output_cache(output_cache_path, layout_signature, output_text);
    prune_cache_family(cache_path, k_cache_max_age_seconds, k_cache_max_layout_files, k_cache_max_seed_files);

    // Cleanup temporary directories for tar files
    for (const auto& dir : input_context.temp_dirs_to_cleanup) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    return 0;
}
