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
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
namespace fs = std::filesystem;
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <system_error>
#include "core/layout_parser.h"
#include "core/cli_parse.h"
#include "core/i18n.h"
#include "core/output_pattern.h"
#include "core/fnv1a.h"
#include <libjsonnet++.h>

namespace {

struct MarkerItem {
    size_t index = 0;
    int sprite_index = -1;
    std::string sprite_name;
    std::string sprite_path;
    std::string name;
    std::string type;
    int x = 0;
    int y = 0;
    int radius = 0;
    int w = 0;
    int h = 0;
    std::vector<std::pair<int, int>> vertices;
};

constexpr int DEFAULT_ANIMATION_FPS = 8;
constexpr int k_default_precision = 8;
constexpr size_t k_string_growth_padding = 8;
constexpr unsigned char k_json_control_char_limit = 0x20;
constexpr std::uint8_t HEX_NIBBLE_MASK = 0x0f;
constexpr int BITS_PER_NIBBLE = 4;
constexpr const char* HEX_DIGITS = "0123456789abcdef";

struct AnimationItem {
    size_t index = 0;
    std::string name;
    std::vector<int> sprite_indexes;
    int fps = DEFAULT_ANIMATION_FPS;
    std::string alias_source;
    std::string flip;
};

using Sprite = sprat::core::Sprite;
using Layout = sprat::core::Layout;
using sprat::core::format_index_pattern;
using sprat::core::parse_int;
using sprat::core::parse_layout;
using sprat::core::parse_pair;
using sprat::core::parse_quoted;
using sprat::core::tr;
using sprat::core::validate_output_pattern;

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }

    return s.substr(start, end - start);
}

bool read_text_file(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Failed to open file: " + path.string();
        return false;
    }
    const auto size = in.tellg();
    if (size < 0) {
        error = "Failed to read file: " + path.string();
        return false;
    }
    in.seekg(0);
    out.resize(static_cast<size_t>(size));
    in.read(out.data(), size);
    if (!in.good() && !in.eof()) {
        error = "Failed to read file: " + path.string();
        return false;
    }
    return true;
}

std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + k_string_growth_padding);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < k_json_control_char_limit) {
                    auto uc = static_cast<unsigned char>(c);
                    out += "\\u00";
                    out += HEX_DIGITS[(uc >> BITS_PER_NIBBLE) & HEX_NIBBLE_MASK];
                    out += HEX_DIGITS[uc & HEX_NIBBLE_MASK];
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string format_double(double value) {
    std::array<char, 32> buf{};
    int n = std::snprintf(buf.data(), buf.size(), "%.*g", k_default_precision, value);
    return std::string(buf.data(), n > 0 ? static_cast<size_t>(n) : 0);
}

std::string sprite_name_from_path(const std::string& path) {
    fs::path p(path);
    std::string name = p.stem().string();
    if (!name.empty()) {
        return name;
    }
    name = p.filename().string();
    if (!name.empty()) {
        return name;
    }
    return path;
}

void collect_sprite_name_indexes(const Layout& layout,
                                 std::unordered_map<std::string, int>& by_path,
                                 std::unordered_map<std::string, int>& by_name,
                                 std::vector<std::string>& sprite_names) {
    by_path.clear();
    by_name.clear();
    sprite_names.clear();
    sprite_names.reserve(layout.sprites.size());
    int idx = 0;
    for (const auto& s : layout.sprites) {
        by_path[s.path] = idx;
        fs::path p(s.path);
        by_path[p.filename().string()] = idx;
        size_t sep = s.path.find('/');
        while (sep != std::string::npos) {
            ++sep;
            std::string suffix = s.path.substr(sep);
            if (!suffix.empty()) {
                by_path.emplace(suffix, idx);
            }
            sep = s.path.find('/', sep);
        }
        std::string name = sprite_name_from_path(s.path);
        sprite_names.push_back(name);
        by_name[name] = idx;
        ++idx;
    }
}

int resolve_sprite_index(const std::string& key,
                         const std::unordered_map<std::string, int>& by_path,
                         const std::unordered_map<std::string, int>& by_name) {
    auto by_path_it = by_path.find(key);
    if (by_path_it != by_path.end()) {
        return by_path_it->second;
    }
    size_t sep = key.find('/');
    while (sep != std::string::npos) {
        ++sep;
        if (sep < key.size()) {
            auto it = by_path.find(key.substr(sep));
            if (it != by_path.end()) {
                return it->second;
            }
        }
        sep = key.find('/', sep);
    }
    auto by_name_it = by_name.find(key);
    if (by_name_it != by_name.end()) {
        return by_name_it->second;
    }
    return -1;
}

std::vector<MarkerItem> parse_markers_data(const std::string& markers_text,
                                           const Layout& layout,
                                           const std::unordered_map<std::string, int>& by_path,
                                           const std::unordered_map<std::string, int>& by_name,
                                           const std::vector<std::string>& sprite_names,
                                           std::vector<std::vector<MarkerItem>>& sprite_markers) {
    sprite_markers.assign(layout.sprites.size(), {});
    std::vector<MarkerItem> markers;
    std::istringstream iss(markers_text);
    std::string line;
    int current_sprite_index = -1;
    std::string raw_root;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || (trimmed.length() > 0 && trimmed[0] == '#')) {
            continue;
        }

        std::istringstream liss(trimmed);
        std::string cmd;
        if (!(liss >> cmd)) {
            continue;
        }

        if (cmd == "root") {
            size_t pos = 4;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                ++pos;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                parse_quoted(trimmed, pos, raw_root, error);
            }
        } else if (cmd == "path") {
            std::string path;
            size_t pos = trimmed.find("path") + 4;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                if (parse_quoted(trimmed, pos, path, error)) {
                    if (!raw_root.empty() && fs::path(path).is_relative()) {
                        path = (fs::path(raw_root) / path).string();
                    }
                    current_sprite_index = resolve_sprite_index(path, by_path, by_name);
                }
            } else {
                if (liss >> path) {
                    if (!raw_root.empty() && fs::path(path).is_relative()) {
                        path = (fs::path(raw_root) / path).string();
                    }
                    current_sprite_index = resolve_sprite_index(path, by_path, by_name);
                }
            }
        } else if (cmd == "-") {
            std::string subcmd;
            if (!(liss >> subcmd) || subcmd != "marker") {
                continue;
            }

            size_t pos = trimmed.find("marker");
            if (pos == std::string::npos) {
                continue;
            }
            pos += 6;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }

            std::string name;
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                if (!parse_quoted(trimmed, pos, name, error)) {
                    continue;
                }
            } else {
                if (!(liss >> name)) {
                    continue;
                }
                pos = trimmed.find(name, pos) + name.length();
            }

            std::string type;
            std::istringstream rest(trimmed.substr(pos));
            if (!(rest >> type)) {
                continue;
            }

            MarkerItem item;
            item.index = markers.size();
            item.sprite_index = current_sprite_index;
            if (current_sprite_index >= 0) {
                item.sprite_name = sprite_names[static_cast<size_t>(current_sprite_index)];
                item.sprite_path = layout.sprites[static_cast<size_t>(current_sprite_index)].path;
            }
            item.name = name;
            item.type = type;

            if (type == "point") {
                std::string pt;
                if (rest >> pt && parse_pair(pt, item.x, item.y)) {
                    // ok
                } else {
                    continue;
                }
            } else if (type == "circle") {
                std::string pt;
                if (rest >> pt && parse_pair(pt, item.x, item.y) && (rest >> item.radius)) {
                    // ok
                } else {
                    continue;
                }
            } else if (type == "rectangle") {
                std::string pt, sz;
                if (rest >> pt && parse_pair(pt, item.x, item.y) && rest >> sz && parse_pair(sz, item.w, item.h)) {
                    // ok
                } else {
                    continue;
                }
            } else if (type == "polygon") {
                std::string pt;
                while (rest >> pt) {
                    int vx = 0;
                    int vy = 0;
                    if (parse_pair(pt, vx, vy)) {
                        item.vertices.emplace_back(vx, vy);
                    }
                }
                if (item.vertices.empty()) {
                    continue;
                }
            } else {
                continue;
            }

            if (current_sprite_index >= 0) {
                sprite_markers[static_cast<size_t>(current_sprite_index)].push_back(item);
            }
            markers.push_back(std::move(item));
        }
    }
    return markers;
}

std::vector<AnimationItem> parse_animations_data(
    const std::string& animations_text,
    const std::unordered_map<std::string, int>& by_path,
    const std::unordered_map<std::string, int>& by_name,
    int& animation_fps_out
) {
    std::vector<AnimationItem> animations;
    std::istringstream iss(animations_text);
    std::string line;
    AnimationItem* current_anim = nullptr;
    std::string raw_root;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || (trimmed.length() > 0 && trimmed[0] == '#')) {
            continue;
        }

        std::istringstream liss(trimmed);
        std::string cmd;
        if (!(liss >> cmd)) {
            continue;
        }

        if (cmd == "root") {
            size_t pos = 4;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                ++pos;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                parse_quoted(trimmed, pos, raw_root, error);
            }
        } else if (cmd == "fps") {
            int fps = 0;
            if (liss >> fps) {
                animation_fps_out = fps;
            }
        } else if (cmd == "animation") {
            std::string name;
            size_t pos = trimmed.find("animation") + 9;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                if (!parse_quoted(trimmed, pos, name, error)) {
                    continue;
                }
            } else {
                if (!(liss >> name)) {
                    continue;
                }
                pos = trimmed.find(name, pos) + name.length();
            }

            AnimationItem item;
            item.index = animations.size();
            item.name = name;
            item.fps = animation_fps_out > 0 ? animation_fps_out : DEFAULT_ANIMATION_FPS;

            std::string next_token;
            {
                std::istringstream rest(trimmed.substr(pos));
                rest >> next_token;
            }

            if (next_token == "alias") {
                size_t alias_kw_pos = trimmed.find("alias", pos);
                size_t alias_src_pos = alias_kw_pos + 5;
                while (alias_src_pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[alias_src_pos]))) {
                    alias_src_pos++;
                }
                if (alias_src_pos < trimmed.size() && trimmed[alias_src_pos] == '"') {
                    std::string error;
                    std::string alias_source;
                    if (parse_quoted(trimmed, alias_src_pos, alias_source, error)) {
                        item.alias_source = alias_source;
                    }
                }
                std::string tok;
                std::istringstream flip_rest(trimmed.substr(alias_src_pos));
                while (flip_rest >> tok) {
                    if (tok == "flip") {
                        std::string val;
                        if (flip_rest >> val) item.flip = val;
                    }
                }
                animations.push_back(std::move(item));
                current_anim = nullptr;
            } else {
                int custom_fps = 0;
                std::istringstream fps_iss(next_token);
                if (fps_iss >> custom_fps) {
                    item.fps = custom_fps;
                }
                animations.push_back(std::move(item));
                current_anim = &animations.back();
            }
        } else if (cmd == "-") {
            std::string subcmd;
            if (!(liss >> subcmd) || subcmd != "frame") {
                continue;
            }
            if (!current_anim) {
                continue;
            }

            size_t pos = trimmed.find("frame") + 5;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string path;
                std::string error;
                if (parse_quoted(trimmed, pos, path, error)) {
                    if (!raw_root.empty() && fs::path(path).is_relative()) {
                        path = (fs::path(raw_root) / path).string();
                    }
                    int idx = resolve_sprite_index(path, by_path, by_name);
                    if (idx >= 0) {
                        current_anim->sprite_indexes.push_back(idx);
                    }
                }
            } else {
                std::string frame_token;
                if (liss >> frame_token) {
                    int idx = -1;
                    if (parse_int(frame_token, idx)) {
                        current_anim->sprite_indexes.push_back(idx);
                    } else {
                        if (!raw_root.empty() && fs::path(frame_token).is_relative()) {
                            frame_token = (fs::path(raw_root) / frame_token).string();
                        }
                        idx = resolve_sprite_index(frame_token, by_path, by_name);
                        if (idx >= 0) {
                            current_anim->sprite_indexes.push_back(idx);
                        }
                    }
                }
            }
        }
    }
    return animations;
}

#ifndef SPRAT_GLOBAL_TRANSFORMS_DIR
#define SPRAT_GLOBAL_TRANSFORMS_DIR "/usr/local/share/sprat/transforms"
#endif

using sprat::core::to_quoted;

fs::path g_exec_dir;

std::optional<fs::path> resolve_user_transforms_dir() {
#ifdef _WIN32
    static const char* const envs[] = {"APPDATA", "LOCALAPPDATA"};
    for (const char* env : envs) {
        const char* val = std::getenv(env);
        if (val != nullptr && val[0] != '\0') {
            const fs::path dir = fs::path(val) / "sprat" / "transforms";
            std::error_code ec;
            if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
                return dir;
            }
        }
    }
    return std::nullopt;
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    const fs::path mac_dir = fs::path(home) / "Library" / "Application Support" / "sprat" / "transforms";
    std::error_code ec_mac;
    if (fs::exists(mac_dir, ec_mac) && fs::is_directory(mac_dir, ec_mac)) {
        return mac_dir;
    }
    return std::nullopt;
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    const fs::path data_dir = (xdg_data_home != nullptr && xdg_data_home[0] != '\0')
        ? fs::path(xdg_data_home) / "sprat" / "transforms"
        : fs::path(home) / ".local" / "share" / "sprat" / "transforms";
    std::error_code ec;
    if (fs::exists(data_dir, ec) && fs::is_directory(data_dir, ec)) {
        return data_dir;
    }
    return std::nullopt;
#endif
}

fs::path find_transforms_dir() {
    std::vector<fs::path> candidates;
    if (!g_exec_dir.empty()) {
        candidates.push_back(g_exec_dir / "transforms");
    }
    if (std::optional<fs::path> user_dir = resolve_user_transforms_dir()) {
        candidates.push_back(*user_dir);
    }
#ifdef SPRAT_SOURCE_DIR
    candidates.push_back(fs::path(SPRAT_SOURCE_DIR) / "transforms");
#endif
    candidates.emplace_back(SPRAT_GLOBAL_TRANSFORMS_DIR);

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            return candidate;
        }
    }

    return fs::path(SPRAT_GLOBAL_TRANSFORMS_DIR);
}

std::string format_atlas_path(const std::string& pattern, int index) {
    if (pattern.empty()) {
        return "";
    }
    std::string out;
    std::string error;
    if (!format_index_pattern(pattern, index, out, error)) {
        return "";
    }
    return out;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string get_animation_name(const std::string& name) {
    std::string anim_name = name;
    while (!anim_name.empty()) {
        char back = anim_name.back();
        if (is_digit(back) || back == '_' || back == '-' || back == ' ' || back == '.' || back == '(' || back == ')') {
            anim_name.pop_back();
        } else {
            break;
        }
    }
    return anim_name;
}

struct GroupMember {
    std::string variant;
    fs::path    path;
};

std::string extract_variant(const std::string& stem) {
    const auto dot_pos = stem.find('.');
    if (dot_pos == std::string::npos) return "";
    return stem.substr(dot_pos + 1);
}

// ─── Jsonnet helpers ──────────────────────────────────────────────────────────

// Build the JSON data string passed as std.extVar("sprat") to all transforms.
std::string build_sprat_json(
    const Layout& layout,
    const std::vector<std::string>& sprite_names,
    const std::vector<MarkerItem>& marker_items,
    const std::vector<AnimationItem>& normalized_animations,
    const std::vector<std::vector<MarkerItem>>& sprite_markers,
    int global_pivot_x,
    int global_pivot_y,
    bool has_global_pivot,
    const std::string& output_pattern_arg,
    const std::string& output_stem,
    const std::string& markers_path_arg,
    const std::string& animations_path_arg,
    int animation_fps)
{
    // Helper: format uint64 as 16-char hex string
    auto to_hex16 = [](uint64_t v) -> std::string {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
        return std::string(buf);
    };

    // Helper: build CSS-safe identifier
    auto to_css_name = [](const std::string& name) -> std::string {
        std::string out;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
                out.push_back(c);
            } else {
                out.push_back('-');
            }
        }
        if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0]))) {
            out.insert(0, 1, '_');
        }
        return out;
    };

    // Helper: build JSON array of marker objects
    auto marker_to_json = [&](const MarkerItem& m) -> std::string {
        std::string o = "{\"name\":\"" + escape_json(m.name) + "\"";
        o += ",\"type\":\"" + escape_json(m.type) + "\"";
        o += ",\"x\":" + std::to_string(m.x);
        o += ",\"y\":" + std::to_string(m.y);
        if (m.type == "circle") {
            o += ",\"radius\":" + std::to_string(m.radius);
        } else if (m.type == "rectangle") {
            o += ",\"w\":" + std::to_string(m.w);
            o += ",\"h\":" + std::to_string(m.h);
        } else if (m.type == "polygon") {
            o += ",\"vertices\":[";
            for (size_t vi = 0; vi < m.vertices.size(); ++vi) {
                if (vi > 0) o += ',';
                o += "{\"x\":" + std::to_string(m.vertices[vi].first);
                o += ",\"y\":" + std::to_string(m.vertices[vi].second) + "}";
            }
            o += "]";
        }
        o += ",\"sprite_index\":" + std::to_string(m.sprite_index);
        o += ",\"sprite_name\":\"" + escape_json(m.sprite_name) + "\"";
        o += ",\"sprite_path\":\"" + escape_json(m.sprite_path) + "\"";
        o += ",\"index\":" + std::to_string(m.index);
        o += "}";
        return o;
    };

    // Helper: build full sprite JSON object
    auto sprite_to_json = [&](size_t i) -> std::string {
        const Sprite& s = layout.sprites[i];
        const std::string& sname = sprite_names[i];

        const int content_w = s.rotated ? s.h : s.w;
        const int content_h = s.rotated ? s.w : s.h;
        const int source_w  = content_w + s.src_x + s.trim_right;
        const int source_h  = content_h + s.src_y + s.trim_bottom;
        const bool has_trim = (s.src_x != 0) || (s.src_y != 0) ||
                              (s.trim_right != 0) || (s.trim_bottom != 0);

        int unity_y = 0;
        if (s.atlas_index >= 0 && static_cast<size_t>(s.atlas_index) < layout.atlases.size()) {
            unity_y = layout.atlases[static_cast<size_t>(s.atlas_index)].height - s.y - s.h;
        }

        int px = has_global_pivot ? global_pivot_x : 0;
        int py = has_global_pivot ? global_pivot_y : 0;
        for (const auto& marker : sprite_markers[i]) {
            if (marker.name == "pivot" && marker.type == "point") {
                px = marker.x;
                py = marker.y;
                break;
            }
        }

        double pivot_x_norm = (source_w > 0) ? (static_cast<double>(px) / source_w) : 0.0;
        double pivot_y_norm = (source_h > 0) ? (1.0 - static_cast<double>(py) / source_h) : 0.0;
        double pivot_y_norm_raw = (source_h > 0) ? (static_cast<double>(py) / source_h) : 0.0;

        const uint64_t nh = sprat::core::fnv1a_hash(
            reinterpret_cast<const unsigned char*>(sname.c_str()), sname.size());
        const std::string nh_hex = to_hex16(nh);
        const std::string nh_dec = std::to_string(nh);

        std::string a_path = format_atlas_path(output_pattern_arg, s.atlas_index);

        std::string o = "{";
        o += "\"index\":" + std::to_string(i);
        o += ",\"name\":\"" + escape_json(sname) + "\"";
        o += ",\"path\":\"" + escape_json(s.path) + "\"";
        o += ",\"atlas_index\":" + std::to_string(s.atlas_index);
        o += ",\"atlas_path\":\"" + escape_json(a_path) + "\"";
        o += ",\"x\":" + std::to_string(s.x);
        o += ",\"y\":" + std::to_string(s.y);
        o += ",\"w\":" + std::to_string(s.w);
        o += ",\"h\":" + std::to_string(s.h);
        o += ",\"trim_left\":" + std::to_string(s.src_x);
        o += ",\"trim_top\":" + std::to_string(s.src_y);
        o += ",\"trim_right\":" + std::to_string(s.trim_right);
        o += ",\"trim_bottom\":" + std::to_string(s.trim_bottom);
        o += ",\"has_trim\":" + std::string(has_trim ? "true" : "false");
        o += ",\"rotated\":" + std::string(s.rotated ? "true" : "false");
        if (s.has_slice) {
            o += ",\"slice_left\":" + std::to_string(s.slice_left);
            o += ",\"slice_top\":" + std::to_string(s.slice_top);
            o += ",\"slice_right\":" + std::to_string(s.slice_right);
            o += ",\"slice_bottom\":" + std::to_string(s.slice_bottom);
            o += ",\"slice_h\":\"" + s.slice_h + "\"";
            o += ",\"slice_v\":\"" + s.slice_v + "\"";
        }
        o += ",\"content_w\":" + std::to_string(content_w);
        o += ",\"content_h\":" + std::to_string(content_h);
        o += ",\"source_w\":" + std::to_string(source_w);
        o += ",\"source_h\":" + std::to_string(source_h);
        o += ",\"unity_y\":" + std::to_string(unity_y);
        o += ",\"pivot_x\":" + std::to_string(px);
        o += ",\"pivot_y\":" + std::to_string(py);
        o += ",\"pivot_x_norm\":" + format_double(pivot_x_norm);
        o += ",\"pivot_y_norm\":" + format_double(pivot_y_norm);
        o += ",\"pivot_y_norm_raw\":" + format_double(pivot_y_norm_raw);
        o += ",\"name_hash_hex\":\"" + nh_hex + "\"";
        o += ",\"name_hash_decimal\":\"" + nh_dec + "\"";
        o += ",\"name_css\":\"" + escape_json(to_css_name(sname)) + "\"";

        // sprite_markers array
        o += ",\"markers\":[";
        const auto& sm = sprite_markers[i];
        for (size_t j = 0; j < sm.size(); ++j) {
            if (j > 0) o += ',';
            o += marker_to_json(sm[j]);
        }
        o += "]";

        // atlas dimensions (for per-sprite access)
        if (s.atlas_index >= 0 && static_cast<size_t>(s.atlas_index) < layout.atlases.size()) {
            o += ",\"atlas_width\":" +
                 std::to_string(layout.atlases[static_cast<size_t>(s.atlas_index)].width);
            o += ",\"atlas_height\":" +
                 std::to_string(layout.atlases[static_cast<size_t>(s.atlas_index)].height);
        } else {
            o += ",\"atlas_width\":0,\"atlas_height\":0";
        }

        o += "}";
        return o;
    };

    // Global hash for output_stem
    const std::string& hash_source = output_pattern_arg.empty() ? output_stem : output_pattern_arg;
    const uint64_t stem_hash = sprat::core::fnv1a_hash(
        reinterpret_cast<const unsigned char*>(hash_source.c_str()), hash_source.size());
    const std::string stem_hash_hex = to_hex16(stem_hash);

    const std::string atlas_path_0 = format_atlas_path(output_pattern_arg, 0);
    const std::string atlas_stem_0 = fs::path(atlas_path_0).stem().string();

    const int eff_fps = animation_fps > 0 ? animation_fps : DEFAULT_ANIMATION_FPS;
    const int atlas_w0 = layout.atlases.empty() ? 0 : layout.atlases[0].width;
    const int atlas_h0 = layout.atlases.empty() ? 0 : layout.atlases[0].height;

    std::string j = "{";

    // Global scalars
    j += "\"atlas_path\":\"" + escape_json(atlas_path_0) + "\"";
    j += ",\"atlas_stem\":\"" + escape_json(atlas_stem_0) + "\"";
    j += ",\"atlas_width\":" + std::to_string(atlas_w0);
    j += ",\"atlas_height\":" + std::to_string(atlas_h0);
    j += ",\"atlas_count\":" + std::to_string(layout.atlases.size());
    j += ",\"multipack\":" + std::string(layout.multipack ? "true" : "false");
    j += ",\"scale\":" + format_double(layout.scale);
    j += ",\"extrude\":" + std::to_string(layout.extrude);
    j += ",\"sprite_count\":" + std::to_string(layout.sprites.size());
    j += ",\"animation_count\":" + std::to_string(normalized_animations.size());
    j += ",\"marker_count\":" + std::to_string(marker_items.size());
    j += ",\"output_pattern\":\"" + escape_json(output_pattern_arg) + "\"";
    j += ",\"output_stem\":\"" + escape_json(output_stem) + "\"";
    j += ",\"output_stem_hash_hex\":\"" + stem_hash_hex + "\"";
    j += ",\"has_animations\":" + std::string(normalized_animations.empty() ? "false" : "true");
    j += ",\"has_markers\":" + std::string(marker_items.empty() ? "false" : "true");
    j += ",\"animations_path\":\"" + escape_json(animations_path_arg) + "\"";
    j += ",\"markers_path\":\"" + escape_json(markers_path_arg) + "\"";
    j += ",\"fps\":" + std::to_string(eff_fps);

    // sprites array (all sprites flat)
    j += ",\"sprites\":[";
    for (size_t i = 0; i < layout.sprites.size(); ++i) {
        if (i > 0) j += ',';
        j += sprite_to_json(i);
    }
    j += "]";

    // atlases array
    j += ",\"atlases\":[";
    for (size_t ai = 0; ai < layout.atlases.size(); ++ai) {
        if (ai > 0) j += ',';
        const auto& atlas = layout.atlases[ai];
        const std::string a_path = format_atlas_path(output_pattern_arg, static_cast<int>(ai));
        j += "{\"index\":" + std::to_string(ai);
        j += ",\"width\":" + std::to_string(atlas.width);
        j += ",\"height\":" + std::to_string(atlas.height);
        j += ",\"path\":\"" + escape_json(a_path) + "\"";
        // sprites in this atlas
        j += ",\"sprites\":[";
        bool first_as = true;
        for (size_t si = 0; si < layout.sprites.size(); ++si) {
            if (layout.sprites[si].atlas_index == static_cast<int>(ai)) {
                if (!first_as) j += ',';
                first_as = false;
                j += sprite_to_json(si);
            }
        }
        j += "]}";
    }
    j += "]";

    // animations array
    j += ",\"animations\":[";
    for (size_t ai = 0; ai < normalized_animations.size(); ++ai) {
        if (ai > 0) j += ',';
        const AnimationItem& anim = normalized_animations[ai];
        const bool is_alias = !anim.alias_source.empty();
        const int eff_anim_fps = anim.fps > 0 ? anim.fps : DEFAULT_ANIMATION_FPS;

        j += "{\"index\":" + std::to_string(ai);
        j += ",\"name\":\"" + escape_json(anim.name) + "\"";
        j += ",\"fps\":" + std::to_string(eff_anim_fps);
        j += ",\"is_alias\":" + std::string(is_alias ? "true" : "false");
        j += ",\"alias_source\":\"" + escape_json(anim.alias_source) + "\"";
        j += ",\"flip\":\"" + escape_json(anim.flip) + "\"";

        // frame_indices
        j += ",\"frame_indices\":[";
        for (size_t fi = 0; fi < anim.sprite_indexes.size(); ++fi) {
            if (fi > 0) j += ',';
            j += std::to_string(anim.sprite_indexes[fi]);
        }
        j += "]";

        // duration
        double dur = anim.sprite_indexes.empty() ? 0.0
            : static_cast<double>(anim.sprite_indexes.size()) / static_cast<double>(eff_anim_fps);
        j += ",\"duration\":" + format_double(dur);

        // frames: resolved sprite info per frame
        j += ",\"frames\":[";
        for (size_t fi = 0; fi < anim.sprite_indexes.size(); ++fi) {
            if (fi > 0) j += ',';
            const int sidx = anim.sprite_indexes[fi];
            const std::string& fname = sprite_names[static_cast<size_t>(sidx)];
            const uint64_t fnh = sprat::core::fnv1a_hash(
                reinterpret_cast<const unsigned char*>(fname.c_str()), fname.size());
            j += "{\"index\":" + std::to_string(sidx);
            j += ",\"name\":\"" + escape_json(fname) + "\"";
            j += ",\"name_hash_hex\":\"" + to_hex16(fnh) + "\"";
            j += ",\"name_hash_decimal\":\"" + std::to_string(fnh) + "\"";
            j += "}";
        }
        j += "]";

        j += "}";
    }
    j += "]";

    // global markers array
    j += ",\"markers\":[";
    for (size_t mi = 0; mi < marker_items.size(); ++mi) {
        if (mi > 0) j += ',';
        j += marker_to_json(marker_items[mi]);
    }
    j += "]";

    j += "}";
    return j;
}

// Evaluate a Jsonnet file with the given sprat JSON data.
// Returns the evaluated output string, or empty string on error (sets error).
std::string evaluate_transform(
    const fs::path& transform_path,
    const std::string& sprat_json,
    std::string& error)
{
    jsonnet::Jsonnet vm;
    if (!vm.init()) {
        error = "Failed to initialize Jsonnet VM";
        return "";
    }
    vm.bindExtCodeVar("sprat", sprat_json);
    // Always add the built-in transforms directory to the import path so that
    // `import "sprat.libsonnet"` resolves from custom transforms outside that dir.
    const fs::path transforms_dir = find_transforms_dir();
    if (!transforms_dir.empty())
        vm.addImportPath(transforms_dir.string());
    std::string output;
    bool ok = vm.evaluateFile(transform_path.string(), &output);
    if (!ok) {
        error = vm.lastError();
        return "";
    }
    return output;
}

// Minimal result returned by a Jsonnet transform.
struct TransformResult {
    std::string name;
    std::string description;
    std::string extension;
    std::string icon;                          // optional relative path to icon
    // Exactly one of content or files is populated:
    std::string content;                        // single-file mode
    struct FileEntry { std::string filename; std::string content; };
    std::vector<FileEntry> files;              // multi-file mode
};

// Unescape a JSON string (assumes well-formed JSON produced by Jsonnet).
static std::string json_unescape_string(const std::string& src, size_t start, size_t end) {
    std::string out;
    out.reserve(end - start);
    for (size_t i = start; i < end; ) {
        if (src[i] == '\\' && i + 1 < end) {
            char next = src[i + 1];
            switch (next) {
                case '"':  out += '"';  i += 2; break;
                case '\\': out += '\\'; i += 2; break;
                case '/':  out += '/';  i += 2; break;
                case 'b':  out += '\b'; i += 2; break;
                case 'f':  out += '\f'; i += 2; break;
                case 'n':  out += '\n'; i += 2; break;
                case 'r':  out += '\r'; i += 2; break;
                case 't':  out += '\t'; i += 2; break;
                case 'u': {
                    if (i + 5 < end) {
                        unsigned int cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = src[i + 2 + k];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(h - 'A' + 10);
                        }
                        // Encode code point as UTF-8
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        i += 6;
                    } else {
                        out += src[i];
                        ++i;
                    }
                    break;
                }
                default:
                    out += src[i];
                    ++i;
                    break;
            }
        } else {
            out += src[i];
            ++i;
        }
    }
    return out;
}

// Find and extract the raw content of a JSON string value for a given key.
// Returns true and sets value_start/value_end (the range inside the quotes) if found.
static bool find_json_string_value(const std::string& json, const std::string& key,
                                   size_t& value_start, size_t& value_end) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while (pos < json.size()) {
        size_t kp = json.find(needle, pos);
        if (kp == std::string::npos) return false;
        pos = kp + needle.size();
        // skip whitespace and colon
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\n' || json[pos] == '\r')) ++pos;
        if (pos >= json.size() || json[pos] != ':') continue;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\n' || json[pos] == '\r')) ++pos;
        if (pos >= json.size() || json[pos] != '"') continue;
        ++pos;
        value_start = pos;
        // scan to end of string value
        while (pos < json.size()) {
            if (json[pos] == '\\') {
                pos += 2;
            } else if (json[pos] == '"') {
                value_end = pos;
                return true;
            } else {
                ++pos;
            }
        }
    }
    return false;
}

// Parse the JSON object output from a Jsonnet transform evaluation.
bool parse_transform_result(const std::string& json, TransformResult& out, std::string& error) {
    // Extract name
    size_t vs = 0, ve = 0;
    if (find_json_string_value(json, "name", vs, ve)) {
        out.name = json_unescape_string(json, vs, ve);
    }
    if (find_json_string_value(json, "description", vs, ve)) {
        out.description = json_unescape_string(json, vs, ve);
    }
    if (find_json_string_value(json, "icon", vs, ve)) {
        out.icon = json_unescape_string(json, vs, ve);
    }
    if (find_json_string_value(json, "extension", vs, ve)) {
        out.extension = json_unescape_string(json, vs, ve);
    } else {
        error = "Transform result missing required field: extension";
        return false;
    }

    // Check for "files" array first — if present we're in multi-file mode and must
    // NOT try to extract "content" from the top level, because "content" also appears
    // as a key inside each files[] entry and find_json_string_value would match that.
    const std::string files_key = "\"files\"";
    size_t fp = json.find(files_key);
    if (fp != std::string::npos) {
        fp += files_key.size();
        // skip to '['
        while (fp < json.size() && json[fp] != '[') ++fp;
        if (fp >= json.size()) {
            error = "Malformed files array in transform result";
            return false;
        }
        ++fp; // skip '['
        while (fp < json.size()) {
            // skip whitespace
            while (fp < json.size() && (json[fp] == ' ' || json[fp] == '\t' ||
                   json[fp] == '\n' || json[fp] == '\r' || json[fp] == ',')) ++fp;
            if (fp >= json.size() || json[fp] == ']') break;
            if (json[fp] != '{') { ++fp; continue; }
            // find filename and content within this object
            size_t obj_end = fp;
            int depth = 0;
            while (obj_end < json.size()) {
                if (json[obj_end] == '{') ++depth;
                else if (json[obj_end] == '}') { --depth; if (depth == 0) { ++obj_end; break; } }
                else if (json[obj_end] == '"') {
                    ++obj_end;
                    while (obj_end < json.size() && json[obj_end] != '"') {
                        if (json[obj_end] == '\\') ++obj_end;
                        ++obj_end;
                    }
                }
                ++obj_end;
            }
            std::string obj_str = json.substr(fp, obj_end - fp);
            TransformResult::FileEntry entry;
            size_t fvs = 0, fve = 0;
            if (find_json_string_value(obj_str, "filename", fvs, fve)) {
                entry.filename = json_unescape_string(obj_str, fvs, fve);
            }
            if (find_json_string_value(obj_str, "content", fvs, fve)) {
                entry.content = json_unescape_string(obj_str, fvs, fve);
            }
            if (!entry.filename.empty()) {
                out.files.push_back(std::move(entry));
            }
            fp = obj_end;
        }
        return true;
    }

    // No "files" key — check for top-level "content" (single-file mode).
    if (find_json_string_value(json, "content", vs, ve)) {
        out.content = json_unescape_string(json, vs, ve);
        return true;
    }

    error = "Transform result must have either 'content' or 'files' field";
    return false;
}

fs::path resolve_transform_path(const std::string& transform_arg) {
    fs::path candidate(transform_arg);
    if (candidate.has_parent_path() || candidate.extension() == ".jsonnet") {
        return candidate;
    }
    return find_transforms_dir() / (transform_arg + ".jsonnet");
}

std::vector<GroupMember> discover_group_transforms(const std::string& group_name,
                                                   const fs::path& transforms_dir) {
    std::vector<GroupMember> members;
    const std::string prefix = group_name + ".";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(transforms_dir, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".jsonnet") continue;
        const std::string stem = entry.path().stem().string();
        if (stem.size() <= prefix.size()) continue;
        if (stem.substr(0, prefix.size()) != prefix) continue;
        members.push_back({stem.substr(prefix.size()), entry.path()});
    }
    std::sort(members.begin(), members.end(),
              [](const GroupMember& a, const GroupMember& b) {
                  return a.variant < b.variant;
              });
    return members;
}

void list_transforms() {
    const fs::path dir = find_transforms_dir();
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".jsonnet") {
            // Skip group members (name.variant.jsonnet); only list top-level names
            const std::string stem = entry.path().stem().string();
            // A group member has the form "group.variant"; skip it
            // We list it only if it has no dot, or if no group prefix exists.
            // Simple heuristic: skip stems containing a dot.
            if (stem.find('.') != std::string::npos) continue;
            paths.push_back(entry.path());
        }
    }
    // Also include group names (unique group prefixes from group.variant.jsonnet files)
    std::vector<std::string> group_names_seen;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".jsonnet") continue;
        const std::string stem = entry.path().stem().string();
        const auto dot_pos = stem.find('.');
        if (dot_pos == std::string::npos) continue;
        const std::string grp = stem.substr(0, dot_pos);
        if (std::find(group_names_seen.begin(), group_names_seen.end(), grp)
                == group_names_seen.end()) {
            group_names_seen.push_back(grp);
        }
    }

    std::ranges::sort(paths);

    // Empty mock data for listing
    const std::string mock_json = R"({"sprites":[],"animations":[],"atlases":[],"markers":[],)"
        R"("atlas_path":"","atlas_stem":"","atlas_width":0,"atlas_height":0,"atlas_count":0,)"
        R"("multipack":false,"scale":1,"extrude":0,"sprite_count":0,"animation_count":0,)"
        R"("marker_count":0,"output_pattern":"","output_stem":"","output_stem_hash_hex":"0000000000000000",)"
        R"("has_animations":false,"has_markers":false,"animations_path":"","markers_path":"","fps":8})";

    for (const auto& path : paths) {
        std::string eval_error;
        std::string output = evaluate_transform(path, mock_json, eval_error);
        if (output.empty()) {
            std::cerr << tr("Warning: ") << eval_error << "\n";
            continue;
        }
        TransformResult result;
        std::string parse_error;
        if (!parse_transform_result(output, result, parse_error)) {
            std::cerr << tr("Warning: ") << parse_error << "\n";
            continue;
        }
        std::cout << result.name;
        if (!result.description.empty()) {
            std::cout << " - " << result.description;
        }
        std::cout << "\n";
        if (!result.icon.empty()) {
            std::cout << "  " << (dir / result.icon).string() << "\n";
        }
    }
    // Print group names
    for (const auto& grp : group_names_seen) {
        std::cout << grp << " (group)\n";
    }
}

void list_transforms_json() {
    const fs::path dir = find_transforms_dir();

    std::vector<fs::path> paths;
    if (fs::exists(dir) && fs::is_directory(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".jsonnet") {
                const std::string stem = entry.path().stem().string();
                if (stem.find('.') != std::string::npos) continue;
                paths.push_back(entry.path());
            }
        }
    }

    std::vector<std::string> group_names_seen;
    if (fs::exists(dir) && fs::is_directory(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".jsonnet") continue;
            const std::string stem = entry.path().stem().string();
            const auto dot_pos = stem.find('.');
            if (dot_pos == std::string::npos) continue;
            const std::string grp = stem.substr(0, dot_pos);
            if (std::find(group_names_seen.begin(), group_names_seen.end(), grp)
                    == group_names_seen.end()) {
                group_names_seen.push_back(grp);
            }
        }
    }

    std::ranges::sort(paths);

    const std::string mock_json = R"({"sprites":[],"animations":[],"atlases":[],"markers":[],)"
        R"("atlas_path":"","atlas_stem":"","atlas_width":0,"atlas_height":0,"atlas_count":0,)"
        R"("multipack":false,"scale":1,"extrude":0,"sprite_count":0,"animation_count":0,)"
        R"("marker_count":0,"output_pattern":"","output_stem":"","output_stem_hash_hex":"0000000000000000",)"
        R"("has_animations":false,"has_markers":false,"animations_path":"","markers_path":"","fps":8})";

    std::cout << "[\n";
    bool first = true;

    for (const auto& path : paths) {
        std::string eval_error;
        std::string output = evaluate_transform(path, mock_json, eval_error);
        if (output.empty()) {
            std::cerr << tr("Warning: ") << eval_error << "\n";
            continue;
        }
        TransformResult result;
        std::string parse_error;
        if (!parse_transform_result(output, result, parse_error)) {
            std::cerr << tr("Warning: ") << parse_error << "\n";
            continue;
        }
        const std::string icon_abs = result.icon.empty()
            ? std::string{}
            : (dir / result.icon).string();

        if (!first) std::cout << ",\n";
        first = false;
        std::cout << "  {\n"
                  << "    \"name\": \""        << escape_json(result.name)        << "\",\n"
                  << "    \"description\": \"" << escape_json(result.description) << "\",\n"
                  << "    \"extension\": \""   << escape_json(result.extension)   << "\",\n"
                  << "    \"icon\": \""        << escape_json(icon_abs)           << "\"\n"
                  << "  }";
    }

    for (const auto& grp : group_names_seen) {
        if (!first) std::cout << ",\n";
        first = false;
        std::cout << "  {\n"
                  << "    \"name\": \""        << escape_json(grp) << "\",\n"
                  << "    \"description\": \"\",\n"
                  << "    \"extension\": \"\",\n"
                  << "    \"icon\": \"\",\n"
                  << "    \"group\": true\n"
                  << "  }";
    }

    std::cout << "\n]\n";
}

void print_usage() {
    std::cout << tr("Usage: spratconvert [OPTIONS]\n")
              << tr("\n")
              << tr("Read layout text from stdin and transform it into other formats.\n")
              << tr("\n")
              << tr("Options:\n")
              << tr("  --transform NAME|PATH      Transform name or path (default: json)\n")
              << tr("  --atlas, -a PATTERN        Atlas path pattern for atlas_* placeholders\n")
              << tr("  --output-dir PATH          Write output to PATH/{variant}{extension} instead of stdout\n")
              << tr("  --list-transforms          Print available transforms and exit\n")
              << tr("  --list-transforms-json     Print available transforms as JSON and exit\n")
              << tr("  --transforms-dir           Print the transforms directory and exit\n")
              << tr("  --markers PATH             Load external markers file\n")
              << tr("  --animations PATH          Load external animations file\n")
              << tr("  --auto-animations          Group frames into animations by name pattern\n")
              << tr("  --help, -h                 Show this help message\n")
              << tr("  --version, -v              Show version\n");
}

} // namespace

int run_spratconvert(int argc, char** argv) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << tr("Failed to set stdout to binary mode\n");
    }
#endif
    g_exec_dir = sprat::core::get_executable_dir(argv[0]);
    std::string transform_arg = "json";
    std::string markers_path_arg;
    std::string animations_path_arg;
    std::string output_pattern_arg;
    std::string output_dir_arg;
    bool list_only = false;
    bool list_json_only = false;
    bool show_transforms_dir = false;
    bool auto_animations = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--transform" && i + 1 < argc) {
            transform_arg = argv[++i];
        } else if ((arg == "--atlas" || arg == "-a" || arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_pattern_arg = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir_arg = argv[++i];
        } else if (arg == "--markers" && i + 1 < argc) {
            markers_path_arg = argv[++i];
        } else if (arg == "--animations" && i + 1 < argc) {
            animations_path_arg = argv[++i];
        } else if (arg == "--auto-animations") {
            auto_animations = true;
        } else if (arg == "--list-transforms") {
            list_only = true;
        } else if (arg == "--list-transforms-json") {
            list_json_only = true;
        } else if (arg == "--transforms-dir") {
            show_transforms_dir = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << tr("spratconvert version ") << SPRAT_VERSION << "\n";
            return 0;
        } else {
            print_usage();
            return 1;
        }
    }

    if (show_transforms_dir) {
        std::cout << find_transforms_dir().string() << "\n";
        return 0;
    }

    if (list_only) {
        list_transforms();
        return 0;
    }

    if (list_json_only) {
        list_transforms_json();
        return 0;
    }

    // Read stdin and parse layout
    std::string input_text;
    {
        input_text.assign(std::istreambuf_iterator<char>(std::cin),
                          std::istreambuf_iterator<char>());
    }
    std::istringstream layout_iss(input_text);
    Layout layout;
    std::string layout_error;
    if (!parse_layout(layout_iss, layout, layout_error)) {
        std::cerr << layout_error << "\n";
        return 1;
    }

    if (output_pattern_arg.empty()) {
        if (layout.multipack || layout.atlases.size() > 1) {
            output_pattern_arg = "atlas_%d.png";
        }
    }
    if (!output_pattern_arg.empty()) {
        std::string pattern_error;
        if (!validate_output_pattern(output_pattern_arg, layout.atlases.size(), true, pattern_error)) {
            std::cerr << tr("Invalid output pattern: ") << pattern_error << "\n";
            return 1;
        }
    }

    std::unordered_map<std::string, int> sprite_index_by_path;
    std::unordered_map<std::string, int> sprite_index_by_name;
    std::vector<std::string> sprite_names;
    collect_sprite_name_indexes(layout, sprite_index_by_path, sprite_index_by_name, sprite_names);

    std::string markers_text;
    std::string animations_text;
    if (!markers_path_arg.empty()) {
        std::string file_error;
        if (!read_text_file(fs::path(markers_path_arg), markers_text, file_error)) {
            std::cerr << file_error << "\n";
            return 1;
        }
    } else {
        markers_text = input_text;
    }
    if (!animations_path_arg.empty()) {
        std::string file_error;
        if (!read_text_file(fs::path(animations_path_arg), animations_text, file_error)) {
            std::cerr << file_error << "\n";
            return 1;
        }
    } else {
        animations_text = input_text;
    }

    std::vector<std::vector<MarkerItem>> sprite_markers;
    const std::vector<MarkerItem> marker_items =
        parse_markers_data(markers_text, layout, sprite_index_by_path, sprite_index_by_name,
                           sprite_names, sprite_markers);
    int animation_fps = -1;
    std::vector<AnimationItem> animation_items =
        parse_animations_data(animations_text, sprite_index_by_path, sprite_index_by_name,
                               animation_fps);

    if (auto_animations) {
        std::map<std::string, std::vector<int>> grouped;
        for (size_t i = 0; i < sprite_names.size(); ++i) {
            std::string anim_name = get_animation_name(sprite_names[i]);
            if (anim_name.empty()) continue;
            grouped[anim_name].push_back(static_cast<int>(i));
        }
        for (auto const& [name, frames] : grouped) {
            if (frames.size() > 1) {
                AnimationItem anim;
                anim.index = animation_items.size();
                anim.name = name;
                anim.sprite_indexes = frames;
                anim.fps = (animation_fps > 0 ? animation_fps : DEFAULT_ANIMATION_FPS);
                animation_items.push_back(std::move(anim));
            }
        }
    }

    const int sprite_count_limit = static_cast<int>(layout.sprites.size());
    std::vector<AnimationItem> normalized_animations = animation_items;
    for (AnimationItem& item : normalized_animations) {
        std::vector<int> filtered;
        filtered.reserve(item.sprite_indexes.size());
        for (int idx : item.sprite_indexes) {
            if (idx >= 0 && idx < sprite_count_limit) {
                filtered.push_back(idx);
            }
        }
        item.sprite_indexes = std::move(filtered);
    }

    int global_pivot_x = 0;
    int global_pivot_y = 0;
    bool has_global_pivot = false;
    for (const auto& marker : marker_items) {
        if (marker.sprite_index < 0 && marker.name == "pivot" && marker.type == "point") {
            global_pivot_x = marker.x;
            global_pivot_y = marker.y;
            has_global_pivot = true;
            break;
        }
    }

    // Mode detection: group vs single
    const bool has_dot = transform_arg.find('.') != std::string::npos;
    bool group_mode = false;
    std::vector<GroupMember> group_members;
    if (!output_dir_arg.empty() && !has_dot) {
        group_members = discover_group_transforms(transform_arg, find_transforms_dir());
        group_mode = !group_members.empty();
    }

    // Helper to compute output_stem from a transform path
    auto compute_output_stem = [](const std::string& targ) -> std::string {
        const std::string stem_str = resolve_transform_path(targ).stem().string();
        std::string stem = extract_variant(stem_str);
        if (stem.empty()) stem = stem_str;
        return stem;
    };

    // Helper to write a single TransformResult to a destination
    auto write_result = [&](const TransformResult& result,
                            const std::string& out_dir,
                            const std::string& file_stem,
                            std::ostream* stdout_out) -> int {
        if (!result.files.empty()) {
            // Multi-file mode: requires --output-dir
            if (out_dir.empty()) {
                std::cerr << tr("Transform produces multiple files; use --output-dir\n");
                return 1;
            }
            std::error_code ec;
            fs::create_directories(out_dir, ec);
            if (ec) {
                std::cerr << tr("Failed to create output directory: ") << ec.message() << "\n";
                return 1;
            }
            int exit_code = 0;
            for (const auto& fe : result.files) {
                const fs::path out_path = fs::path(out_dir) / fe.filename;
                std::ofstream out_file(out_path, std::ios::binary);
                if (!out_file) {
                    std::cerr << tr("Failed to open output file: ") << out_path.string() << "\n";
                    exit_code = 1;
                    continue;
                }
                out_file << fe.content;
            }
            return exit_code;
        }

        // Single content mode
        if (!out_dir.empty()) {
            std::error_code ec;
            fs::create_directories(out_dir, ec);
            if (ec) {
                std::cerr << tr("Failed to create output directory: ") << ec.message() << "\n";
                return 1;
            }
            const std::string out_filename = file_stem + result.extension;
            const fs::path out_path = fs::path(out_dir) / out_filename;
            std::ofstream out_file(out_path, std::ios::binary);
            if (!out_file) {
                std::cerr << tr("Failed to open output file: ") << out_path.string() << "\n";
                return 1;
            }
            out_file << result.content;
            return 0;
        }

        if (stdout_out) {
            *stdout_out << result.content;
        }
        return 0;
    };

    if (group_mode) {
        std::error_code ec;
        fs::create_directories(output_dir_arg, ec);
        if (ec) {
            std::cerr << tr("Failed to create output directory: ") << ec.message() << "\n";
            return 1;
        }
        int exit_code = 0;
        for (const GroupMember& member : group_members) {
            const std::string sprat_json = build_sprat_json(
                layout, sprite_names, marker_items, normalized_animations, sprite_markers,
                global_pivot_x, global_pivot_y, has_global_pivot,
                output_pattern_arg, member.variant,
                markers_path_arg, animations_path_arg, animation_fps);

            std::string eval_error;
            std::string output = evaluate_transform(member.path, sprat_json, eval_error);
            if (output.empty() && !eval_error.empty()) {
                std::cerr << eval_error << "\n";
                exit_code = 1;
                continue;
            }

            TransformResult result;
            std::string parse_error;
            if (!parse_transform_result(output, result, parse_error)) {
                std::cerr << parse_error << "\n";
                exit_code = 1;
                continue;
            }

            const int r = write_result(result, output_dir_arg, member.variant, nullptr);
            if (r != 0) exit_code = r;
        }
        return exit_code;
    }

    // Single transform mode
    const fs::path transform_path = resolve_transform_path(transform_arg);
    const std::string output_stem = !output_dir_arg.empty()
        ? compute_output_stem(transform_arg)
        : "";

    const std::string sprat_json = build_sprat_json(
        layout, sprite_names, marker_items, normalized_animations, sprite_markers,
        global_pivot_x, global_pivot_y, has_global_pivot,
        output_pattern_arg, output_stem,
        markers_path_arg, animations_path_arg, animation_fps);

    std::string eval_error;
    std::string output = evaluate_transform(transform_path, sprat_json, eval_error);
    if (output.empty() && !eval_error.empty()) {
        std::cerr << eval_error << "\n";
        return 1;
    }

    TransformResult result;
    std::string parse_error;
    if (!parse_transform_result(output, result, parse_error)) {
        std::cerr << parse_error << "\n";
        return 1;
    }

    return write_result(result, output_dir_arg, output_stem, &std::cout);
}
