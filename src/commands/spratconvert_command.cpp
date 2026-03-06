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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
namespace fs = std::filesystem;
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "core/layout_parser.h"

namespace {
struct Transform {
    std::string name;
    std::string description;
    std::string extension;
    std::string header;
    std::string if_markers;
    std::string if_no_markers;
    std::string markers_header;
    std::string markers;
    std::string markers_separator;
    std::string markers_footer;
    std::string sprite;
    std::string sprite_markers_header;
    std::string sprite_marker;
    std::string sprite_markers_separator;
    std::string sprite_markers_footer;
    std::string separator;
    std::string if_animations;
    std::string if_no_animations;
    std::string animations_header;
    std::string animations;
    std::string animations_separator;
    std::string animations_footer;
    std::string atlas_header;
    std::string atlas;
    std::string atlas_separator;
    std::string atlas_footer;
    std::string footer;
};

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
constexpr size_t k_token_replacement_reserve_extra = 64;
constexpr std::uint8_t HEX_NIBBLE_MASK = 0x0f;
constexpr int BITS_PER_NIBBLE = 4;
constexpr const char* HEX_DIGITS = "0123456789abcdef";

struct AnimationItem {
    size_t index = 0;
    std::string name;
    std::vector<int> sprite_indexes;
    int fps = DEFAULT_ANIMATION_FPS;
};

using Sprite = sprat::core::Sprite;
using Layout = sprat::core::Layout;
using sprat::core::parse_int;
using sprat::core::parse_layout;
using sprat::core::parse_pair;
using sprat::core::parse_quoted;

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s.at(start))) != 0) {
        ++start;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s.at(end - 1))) != 0) {
        --end;
    }

    return s.substr(start, end - start);
}

bool read_text_file(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open file: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        error = "Failed to read file: " + path.string();
        return false;
    }
    out = buffer.str();
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
                    std::ostringstream hex;
                    hex << "\\u";
                    auto uc = static_cast<unsigned char>(c);
                    hex << '0' << '0' << HEX_DIGITS[(uc >> BITS_PER_NIBBLE) & HEX_NIBBLE_MASK] << HEX_DIGITS[uc & HEX_NIBBLE_MASK];
                    out += hex.str();
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string escape_xml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + k_string_growth_padding);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string escape_csv(const std::string& s) {
    bool needs_quotes = false;
    for (char c : s) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return s;
    }

    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string escape_css_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + k_string_growth_padding);
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        if (c == '\n') {
            out += "\\a ";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

enum class PlaceholderEncoding {
    none,
    json,
    xml,
    csv,
    css
};

std::string escape_value(const std::string& value, PlaceholderEncoding encoding) {
    switch (encoding) {
        case PlaceholderEncoding::json: return escape_json(value);
        case PlaceholderEncoding::xml: return escape_xml(value);
        case PlaceholderEncoding::csv: return escape_csv(value);
        case PlaceholderEncoding::css: return escape_css_string(value);
        default: return value;
    }
}

std::string filter_sections_by_attr(const std::string& input,
                                    const std::map<std::string, std::string>& vars,
                                    PlaceholderEncoding encoding) {
    std::string output;
    size_t pos = 0;
    auto encoding_to_string = [](PlaceholderEncoding enc) {
        switch (enc) {
            case PlaceholderEncoding::json: return std::string("json");
            case PlaceholderEncoding::xml: return std::string("xml");
            case PlaceholderEncoding::csv: return std::string("csv");
            case PlaceholderEncoding::css: return std::string("css");
            default: return std::string();
        }
    };
    const std::string encoding_name = encoding_to_string(encoding);

    while (pos < input.size()) {
        size_t start = input.find('[', pos);
        if (start == std::string::npos) {
            output.append(input.substr(pos));
            break;
        }
        output.append(input.substr(pos, start - pos));
        if (start + 1 >= input.size() || input[start + 1] == '/') {
            output.push_back(input[start]);
            pos = start + 1;
            continue;
        }
        size_t header_end = input.find(']', start + 1);
        if (header_end == std::string::npos) {
            output.append(input.substr(start));
            break;
        }
        std::string header = input.substr(start + 1, header_end - start - 1);
        std::string tag;
        std::string attr;
        std::string value;
        size_t i = 0;
        while (i < header.size() && !std::isspace(static_cast<unsigned char>(header[i]))) {
            tag.push_back(header[i]);
            ++i;
        }
        while (i < header.size()) {
            while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i]))) {
                ++i;
            }
            size_t name_start = i;
            while (i < header.size() && header[i] != '=' && !std::isspace(static_cast<unsigned char>(header[i]))) {
                ++i;
            }
            std::string attr_name = header.substr(name_start, i - name_start);
            while (i < header.size() && header[i] != '=') {
                ++i;
            }
            if (i >= header.size() || header[i] != '=') {
                break;
            }
            ++i;
            while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i]))) {
                ++i;
            }
            if (i >= header.size() || header[i] != '"') {
                break;
            }
            ++i;
            size_t value_start = i;
            while (i < header.size() && header[i] != '"') {
                ++i;
            }
            std::string attr_value = header.substr(value_start, i - value_start);
            ++i;
            if (attr_name == "type" || attr_name == "marker_type") {
                attr = attr_name;
                value = attr_value;
                break;
            }
        }
        size_t close = input.find("[/" + tag + "]", header_end + 1);
        if (close == std::string::npos) {
            output.append(input.substr(start));
            break;
        }
        bool keep = true;
        if (!attr.empty()) {
            if (attr == "type") {
                keep = (value == encoding_name);
            } else {
                auto it = vars.find(attr);
                keep = (it != vars.end() && it->second == value);
            }
        }
        if (!keep) {
            pos = close + tag.size() + 3;
            continue;
        }
        output.append(input.substr(header_end + 1, close - header_end - 1));
        pos = close + tag.size() + 3;
    }

    return output;
}

std::string filter_rotated_sections(const std::string& input, bool rotated) {
    std::string output;
    size_t pos = 0;
    while (pos < input.size()) {
        size_t start = input.find("[rotated]", pos);
        if (start == std::string::npos) {
            output.append(input.substr(pos));
            break;
        }
        output.append(input.substr(pos, start - pos));
        size_t end = input.find("[/rotated]", start + 9);
        if (end == std::string::npos) {
            break;
        }
        if (rotated) {
            output.append(input.substr(start + 9, end - start - 9));
        }
        pos = end + 10;
    }
    return output;
}

std::string to_lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool has_suffix(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

PlaceholderEncoding detect_placeholder_encoding(const Transform& transform,
                                                const std::string& transform_arg) {
    const auto from_token = [](const std::string& token) -> PlaceholderEncoding {
        std::string normalized = to_lower_copy(token);
        if (!normalized.empty() && normalized.front() == '.') {
            normalized.erase(normalized.begin());
        }
        if (normalized == "json") {
            return PlaceholderEncoding::json;
        }
        if (normalized == "xml") {
            return PlaceholderEncoding::xml;
        }
        if (normalized == "csv") {
            return PlaceholderEncoding::csv;
        }
        if (normalized == "css") {
            return PlaceholderEncoding::css;
        }
        return PlaceholderEncoding::none;
    };

    if (PlaceholderEncoding from_meta = from_token(transform.extension);
        from_meta != PlaceholderEncoding::none) {
        return from_meta;
    }
    if (PlaceholderEncoding from_name = from_token(transform.name);
        from_name != PlaceholderEncoding::none) {
        return from_name;
    }
    return from_token(transform_arg);
}

std::string replace_tokens(const std::string& input,
                           const std::map<std::string, std::string>& vars,
                           PlaceholderEncoding encoding) {
    bool rotated = false;
    auto rotated_it = vars.find("rotated");
    if (rotated_it != vars.end()) {
        rotated = (rotated_it->second == "true");
    }
    std::string filtered = filter_rotated_sections(input, rotated);
    filtered = filter_sections_by_attr(filtered, vars, encoding);
    std::string out;
    out.reserve(filtered.size() + k_token_replacement_reserve_extra);

    auto is_composite_variable = [](std::string_view key) {
        return key == "sprites" || key == "markers" || key == "animations" || 
               key == "sprite_markers" || key == "atlases" || key == "sprite_indexes" || 
               key == "vertices";
    };

    size_t i = 0;
    while (i < filtered.size()) {
        size_t open = filtered.find("{{", i);
        if (open == std::string::npos) {
            out.append(filtered.substr(i));
            break;
        }

        out.append(filtered.substr(i, open - i));
        
        bool is_raw = false;
        size_t close;
        std::string key;
        
        if (open + 2 < filtered.size() && filtered[open + 2] == '{') {
            is_raw = true;
            close = filtered.find("}}}", open + 3);
            if (close == std::string::npos) {
                out.append(filtered.substr(open));
                break;
            }
            key = trim_copy(filtered.substr(open + 3, close - (open + 3)));
            i = close + 3;
        } else {
            close = filtered.find("}}", open + 2);
            if (close == std::string::npos) {
                out.append(filtered.substr(open));
                break;
            }
            key = trim_copy(filtered.substr(open + 2, close - (open + 2)));
            i = close + 2;
        }

        auto it = vars.find(key);
        if (it != vars.end()) {
            PlaceholderEncoding entry_encoding = (is_raw || is_composite_variable(key)) ? PlaceholderEncoding::none : encoding;
            out.append(escape_value(it->second, entry_encoding));
        }
    }

    return out;
}

std::string format_sprite_indexes(const std::vector<int>& values, PlaceholderEncoding encoding) {
    if (values.empty()) {
        return (encoding == PlaceholderEncoding::json) ? "[]" : "";
    }
    std::ostringstream oss;
    if (encoding == PlaceholderEncoding::json) {
        oss << "[";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) oss << ",";
            oss << values[i];
        }
        oss << "]";
    } else if (encoding == PlaceholderEncoding::csv) {
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) oss << "|";
            oss << values[i];
        }
    } else {
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) oss << ",";
            oss << values[i];
        }
    }
    return oss.str();
}

std::string format_markers_json(const std::vector<MarkerItem>& markers) {
    std::ostringstream oss;
    oss << "[";
    bool first_marker = true;
    for (const auto& marker : markers) {
        if (!first_marker) {
            oss << ",";
        }
        oss << R"({"name":")" << escape_json(marker.name) << R"(","type":")" << escape_json(marker.type) << "\"";
        if (marker.type == "point") {
            oss << ",\"x\":" << marker.x << ",\"y\":" << marker.y;
        } else if (marker.type == "circle") {
            oss << ",\"x\":" << marker.x << ",\"y\":" << marker.y << ",\"radius\":" << marker.radius;
        } else if (marker.type == "rectangle") {
            oss << ",\"x\":" << marker.x << ",\"y\":" << marker.y << ",\"w\":" << marker.w << ",\"h\":" << marker.h;
        } else if (marker.type == "polygon") {
            oss << ",\"vertices\":[";
            bool first_vertex = true;
            for (const auto& vertex : marker.vertices) {
                if (!first_vertex) {
                    oss << ",";
                }
                oss << "{\"x\":" << vertex.first << ",\"y\":" << vertex.second << "}";
                first_vertex = false;
            }
            oss << "]";
        }
        oss << "}";
        first_marker = false;
    }
    oss << "]";
    return oss.str();
}

std::string format_vertices(const std::vector<std::pair<int, int>>& vertices, PlaceholderEncoding encoding) {
    if (vertices.empty()) {
        return (encoding == PlaceholderEncoding::json) ? "[]" : "";
    }
    std::ostringstream oss;
    if (encoding == PlaceholderEncoding::json) {
        oss << "[";
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"x\":" << vertices[i].first << ",\"y\":" << vertices[i].second << "}";
        }
        oss << "]";
    } else {
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (i > 0) oss << "|";
            oss << vertices[i].first << "," << vertices[i].second;
        }
    }
    return oss.str();
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

        if (cmd == "path") {
            std::string path;
            size_t pos = trimmed.find("path") + 4;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string error;
                if (parse_quoted(trimmed, pos, path, error)) {
                    current_sprite_index = resolve_sprite_index(path, by_path, by_name);
                }
            } else {
                if (liss >> path) {
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

        if (cmd == "fps") {
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

            int fps = animation_fps_out > 0 ? animation_fps_out : DEFAULT_ANIMATION_FPS;
            int custom_fps = 0;
            std::istringstream rest(trimmed.substr(pos));
            if (rest >> custom_fps) {
                fps = custom_fps;
            }

            AnimationItem item;
            item.index = animations.size();
            item.name = name;
            item.fps = fps;
            animations.push_back(std::move(item));
            current_anim = &animations.back();
        } else if (cmd == "-") {
            std::string subcmd;
            if (!(liss >> subcmd) || subcmd != "frame") {
                continue;
            }
            if (!current_anim) {
                continue;
            }

            std::string frame_token;
            size_t pos = trimmed.find("frame") + 5;
            while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                pos++;
            }
            if (pos < trimmed.size() && trimmed[pos] == '"') {
                std::string path;
                std::string error;
                if (parse_quoted(trimmed, pos, path, error)) {
                    int idx = resolve_sprite_index(path, by_path, by_name);
                    if (idx >= 0) {
                        current_anim->sprite_indexes.push_back(idx);
                    }
                }
            } else {
                if (liss >> frame_token) {
                    int idx = -1;
                    if (parse_int(frame_token, idx)) {
                        current_anim->sprite_indexes.push_back(idx);
                    } else {
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


bool parse_transform_file(const fs::path& path, Transform& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "Failed to open transform file: " + path.string();
        return false;
    }

    Transform parsed;
    std::vector<std::string> section_stack;
    std::string line;
    std::string legacy_sprite_block;
    std::string legacy_marker_block;
    std::string legacy_animation_block;
    bool saw_sprite_item = false;
    bool saw_marker_item = false;
    bool saw_animation_item = false;

    auto append_line = [&](std::string& target, const std::string& value) {
        target.append(value);
        target.push_back('\n');
    };

    auto is_known_section = [](const std::string& s) {
        return s == "meta"
            || s == "header"
            || s == "if_markers"
            || s == "if_no_markers"
            || s == "markers_header"
            || s == "markers"
            || s == "marker"
            || s == "markers_separator"
            || s == "markers_footer"
            || s == "sprites"
            || s == "sprite"
            || s == "sprite_markers_header"
            || s == "sprite_marker"
            || s == "sprite_markers_separator"
            || s == "sprite_markers_footer"
            || s == "separator"
            || s == "if_animations"
            || s == "if_no_animations"
            || s == "animations_header"
            || s == "animations"
            || s == "animation"
            || s == "animations_separator"
            || s == "animations_footer"
            || s == "atlases"
            || s == "atlas_header"
            || s == "atlas"
            || s == "atlas_separator"
            || s == "atlas_footer"
            || s == "footer";
    };

    bool dsl_mode = false;
    while (std::getline(in, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() && section_stack.empty()) {
            continue;
        }

        if (!trimmed.empty() && trimmed.front() == '#') {
            continue;
        }

        bool section_tag = false;
        if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string full_tag = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            if (!full_tag.empty() && full_tag.front() == '/') {
                std::string tag = trim_copy(full_tag.substr(1));
                if (is_known_section(tag) && !section_stack.empty() && tag == section_stack.back()) {
                    section_stack.pop_back();
                    section_tag = true;
                    dsl_mode = false;
                }
            } else {
                std::string tag;
                size_t space_pos = full_tag.find_first_of(" \t\r\n");
                if (space_pos != std::string::npos) {
                    tag = full_tag.substr(0, space_pos);
                } else {
                    tag = full_tag;
                }
                
                if (is_known_section(tag)) {
                    // Only treat as a section if there are no attributes
                    if (space_pos == std::string::npos) {
                        if (tag == "sprite") {
                            if (section_stack.empty() || (section_stack.back() != "sprites" && section_stack.back() != "atlas")) {
                                // Auto-open sprites if sprite is used without it
                                section_stack.push_back("sprites");
                            }
                            saw_sprite_item = true;
                        } else if (tag == "marker") {
                            if (section_stack.empty() || section_stack.back() != "markers") {
                                section_stack.push_back("markers");
                            }
                            saw_marker_item = true;
                        } else if (tag == "animation") {
                            if (section_stack.empty() || section_stack.back() != "animations") {
                                section_stack.push_back("animations");
                            }
                            saw_animation_item = true;
                        } else if (tag == "atlas") {
                            if (section_stack.empty() || section_stack.back() != "atlases") {
                                section_stack.push_back("atlases");
                            }
                        }
                        section_stack.push_back(tag);
                        section_tag = true;
                        dsl_mode = false;
                    }
                }
            }
        }

        if (section_tag) {
            continue;
        }

        if (section_stack.empty()) {
            std::istringstream liss(trimmed);
            std::string cmd;
            if (liss >> cmd) {
                if (cmd == "-") {
                    std::string subcmd;
                    if (liss >> subcmd) {
                        if (is_known_section(subcmd)) {
                            dsl_mode = true;
                            if (subcmd == "sprite") {
                                if (section_stack.empty() || section_stack.back() != "sprites") {
                                    section_stack.push_back("sprites");
                                }
                                saw_sprite_item = true;
                            } else if (subcmd == "marker") {
                                if (section_stack.empty() || section_stack.back() != "markers") {
                                    section_stack.push_back("markers");
                                }
                                saw_marker_item = true;
                            } else if (subcmd == "animation") {
                                if (section_stack.empty() || section_stack.back() != "animations") {
                                    section_stack.push_back("animations");
                                }
                                saw_animation_item = true;
                            }
                            section_stack.push_back(subcmd);
                            continue;
                        }
                    }
                } else if (is_known_section(cmd)) {
                    // Start section
                    dsl_mode = true;
                    section_stack.push_back(cmd);
                    
                    // If it's meta, we might have arguments on the same line
                    if (cmd == "meta") {
                        std::string rest;
                        if (std::getline(liss, rest)) {
                            std::string trimmed_rest = trim_copy(rest);
                            if (!trimmed_rest.empty()) {
                                size_t eq = trimmed_rest.find('=');
                                if (eq != std::string::npos) {
                                    std::string key = trim_copy(trimmed_rest.substr(0, eq));
                                    std::string value = trim_copy(trimmed_rest.substr(eq + 1));
                                    if (key == "name") parsed.name = value;
                                    else if (key == "description") parsed.description = value;
                                    else if (key == "extension") parsed.extension = value;
                                }
                            }
                        }
                    }
                    continue;
                }
            }
        } else if (dsl_mode) {
            // Check for new section starting without [tag], auto-closing previous
            std::istringstream liss(trimmed);
            std::string cmd;
            if (liss >> cmd) {
                if (cmd == "-") {
                    std::string subcmd;
                    if (liss >> subcmd && is_known_section(subcmd)) {
                        // Pop until we find where it belongs or just pop current if it's a sibling/new level
                        if (subcmd == "sprite" || subcmd == "marker" || subcmd == "animation" || subcmd == "atlas" || subcmd == "atlases" || subcmd == "sprite_marker" || 
                            subcmd == "sprite_markers_header" || subcmd == "sprite_markers_separator" || subcmd == "sprite_markers_footer") {
                            // These can be nested. If we are in the parent, stay. If we are in another sibling, pop.
                            std::string parent;
                            if (subcmd == "sprite") {
                                if (!section_stack.empty() && section_stack.back() == "atlas") parent = "atlas";
                                else parent = "sprites";
                            }
                            else if (subcmd == "marker") parent = "markers";
                            else if (subcmd == "animation") parent = "animations";
                            else if (subcmd == "atlas") parent = "atlases";
                            else if (subcmd == "sprite_marker") parent = "sprite";
                            else if (subcmd == "sprite_markers_header") parent = "sprite";
                            else if (subcmd == "sprite_markers_separator") parent = "sprite";
                            else if (subcmd == "sprite_markers_footer") parent = "sprite";

                            while (!section_stack.empty() && section_stack.back() != parent && !parent.empty()) {
                                section_stack.pop_back();
                            }
                            if (section_stack.empty() && !parent.empty()) section_stack.push_back(parent);
                            if (subcmd == "sprite") saw_sprite_item = true;
                            else if (subcmd == "marker") saw_marker_item = true;
                            else if (subcmd == "animation") saw_animation_item = true;
                            section_stack.push_back(subcmd);
                            continue;
                        } else {
                            while (!section_stack.empty()) section_stack.pop_back();
                            section_stack.push_back(subcmd);
                            continue;
                        }
                    }
                } else if (is_known_section(cmd)) {
                    while (!section_stack.empty()) section_stack.pop_back();
                    section_stack.push_back(cmd);
                    if (cmd == "meta") {
                        std::string rest;
                        if (std::getline(liss, rest)) {
                            std::string trimmed_rest = trim_copy(rest);
                            if (!trimmed_rest.empty()) {
                                size_t eq = trimmed_rest.find('=');
                                if (eq != std::string::npos) {
                                    std::string key = trim_copy(trimmed_rest.substr(0, eq));
                                    std::string value = trim_copy(trimmed_rest.substr(eq + 1));
                                    if (key == "name") parsed.name = value;
                                    else if (key == "description") parsed.description = value;
                                    else if (key == "extension") parsed.extension = value;
                                }
                            }
                        }
                    }
                    continue;
                }
            }
        }

        if (section_stack.empty()) {
            continue;
        }

        const std::string section = section_stack.back();

        if (section == "meta") {
            size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = trim_copy(line.substr(0, eq));
            std::string value = trim_copy(line.substr(eq + 1));
            if (key == "name") {
                parsed.name = value;
            } else if (key == "description") {
                parsed.description = value;
            } else if (key == "extension") {
                parsed.extension = value;
            }
            continue;
        }

        if (section == "header") {
            append_line(parsed.header, line);
        } else if (section == "if_markers") {
            append_line(parsed.if_markers, line);
        } else if (section == "if_no_markers") {
            append_line(parsed.if_no_markers, line);
        } else if (section == "markers_header") {
            append_line(parsed.markers_header, line);
        } else if (section == "markers") {
            append_line(legacy_marker_block, line);
        } else if (section == "marker") {
            append_line(parsed.markers, line);
        } else if (section == "markers_separator") {
            append_line(parsed.markers_separator, line);
        } else if (section == "markers_footer") {
            append_line(parsed.markers_footer, line);
        } else if (section == "sprites") {
            append_line(legacy_sprite_block, line);
        } else if (section == "sprite") {
            append_line(parsed.sprite, line);
        } else if (section == "sprite_markers_header") {
            append_line(parsed.sprite_markers_header, line);
        } else if (section == "sprite_marker") {
            append_line(parsed.sprite_marker, line);
        } else if (section == "sprite_markers_separator") {
            append_line(parsed.sprite_markers_separator, line);
        } else if (section == "sprite_markers_footer") {
            append_line(parsed.sprite_markers_footer, line);
        } else if (section == "separator") {
            append_line(parsed.separator, line);
        } else if (section == "if_animations") {
            append_line(parsed.if_animations, line);
        } else if (section == "if_no_animations") {
            append_line(parsed.if_no_animations, line);
        } else if (section == "animations_header") {
            append_line(parsed.animations_header, line);
        } else if (section == "animations") {
            append_line(legacy_animation_block, line);
        } else if (section == "animation") {
            append_line(parsed.animations, line);
        } else if (section == "animations_separator") {
            append_line(parsed.animations_separator, line);
        } else if (section == "animations_footer") {
            append_line(parsed.animations_footer, line);
        } else if (section == "atlas_header") {
            append_line(parsed.atlas_header, line);
        } else if (section == "atlas") {
            append_line(parsed.atlas, line);
        } else if (section == "atlas_separator") {
            append_line(parsed.atlas_separator, line);
        } else if (section == "atlas_footer") {
            append_line(parsed.atlas_footer, line);
        } else if (section == "footer") {
            append_line(parsed.footer, line);
        }
    }

    if (dsl_mode) {
        section_stack.clear();
    }

    if (!section_stack.empty()) {
        error = "Unclosed section [" + section_stack.back() + "]: " + path.string();
        return false;
    }

    if (!saw_sprite_item) {
        parsed.sprite = legacy_sprite_block;
    }
    if (!saw_marker_item) {
        parsed.markers = legacy_marker_block;
    }
    if (!saw_animation_item) {
        parsed.animations = legacy_animation_block;
    }

    if (parsed.name.empty()) {
        parsed.name = path.stem().string();
    }
    if (parsed.sprite.empty()) {
        error = "Transform missing [sprite] section (or legacy [sprites] body): " + path.string();
        return false;
    }

    out = std::move(parsed);
    return true;
}

std::string format_double(double value) {
    std::ostringstream oss;
    oss.unsetf(std::ios::floatfield);
    oss.precision(k_default_precision);
    oss << value;
    return oss.str();
}

fs::path find_transforms_dir() {
    std::vector<fs::path> candidates;
    candidates.emplace_back("transforms");
#ifdef SPRAT_SOURCE_DIR
    candidates.push_back(fs::path(SPRAT_SOURCE_DIR) / "transforms");
#endif

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return fs::path("transforms");
}

fs::path resolve_transform_path(const std::string& transform_arg) {
    fs::path candidate(transform_arg);
    if (candidate.has_parent_path() || candidate.extension() == ".transform") {
        return candidate;
    }
    return find_transforms_dir() / (transform_arg + ".transform");
}

bool load_transform_by_name(const std::string& transform_arg, Transform& out, std::string& error) {
    fs::path transform_path = resolve_transform_path(transform_arg);
    return parse_transform_file(transform_path, out, error);
}

void list_transforms() {
    const fs::path dir = find_transforms_dir();
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".transform") {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);

    for (const auto& path : paths) {
        Transform t;
        std::string error;
        if (!parse_transform_file(path, t, error)) {
            std::cerr << "Warning: " << error << "\n";
            continue;
        }
        std::cout << t.name;
        if (!t.description.empty()) {
            std::cout << " - " << t.description;
        }
        std::cout << "\n";
    }
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

std::string format_atlas_path(const std::string& pattern, int index) {
    if (pattern.empty()) {
        return "";
    }
    char buf[1024];
    int written = 0;
#ifdef _WIN32
    written = _snprintf(buf, sizeof(buf), pattern.c_str(), index);
#else
    written = snprintf(buf, sizeof(buf), pattern.c_str(), index);
#endif
    if (written < 0 || static_cast<size_t>(written) >= sizeof(buf)) {
        return "";
    }
    return std::string(buf);
}

void print_usage() {
    std::cout << "Usage: spratconvert [OPTIONS]\n"
              << "\n"
              << "Read layout text from stdin and transform it into other formats.\n"
              << "Unsuffixed placeholders are auto-encoded based on transform output type.\n"
              << "\n"
              << "Options:\n"
              << "  --transform NAME|PATH      Transform name or path (default: json)\n"
              << "  --output, -o PATTERN       Atlas filename pattern (e.g. atlas_%d.png)\n"
              << "  --list-transforms          Print available transforms and exit\n"
              << "  --markers PATH             Load external markers file\n"
              << "  --animations PATH          Load external animations file\n"
              << "  --auto-animations          Group frames into animations by name pattern\n"
              << "  --help, -h                 Show this help message\n";
}
} // namespace

int run_spratconvert(int argc, char** argv) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "Failed to set stdout to binary mode\n";
    }
#endif
    std::string transform_arg = "json";
    std::string markers_path_arg;
    std::string animations_path_arg;
    std::string output_pattern_arg;
    bool list_only = false;
    bool auto_animations = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--transform" && i + 1 < argc) {
            transform_arg = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_pattern_arg = argv[++i];
        } else if (arg == "--markers" && i + 1 < argc) {
            markers_path_arg = argv[++i];
        } else if (arg == "--animations" && i + 1 < argc) {
            animations_path_arg = argv[++i];
        } else if (arg == "--auto-animations") {
            auto_animations = true;
        } else if (arg == "--list-transforms") {
            list_only = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "spratconvert version " << SPRAT_VERSION << "\n";
            return 0;
        } else {
            print_usage();
            return 1;
        }
    }

    if (list_only) {
        list_transforms();
        return 0;
    }

    Transform transform;
    std::string transform_error;
    if (!load_transform_by_name(transform_arg, transform, transform_error)) {
        std::cerr << transform_error << "\n";
        return 1;
    }
    const PlaceholderEncoding placeholder_encoding =
        detect_placeholder_encoding(transform, transform_arg);

    std::string input_text;
    {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        input_text = buffer.str();
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
        parse_markers_data(markers_text, layout, sprite_index_by_path, sprite_index_by_name, sprite_names, sprite_markers);
    int animation_fps = -1;
    std::vector<AnimationItem> animation_items =
        parse_animations_data(animations_text, sprite_index_by_path, sprite_index_by_name, animation_fps);

    if (auto_animations) {
        std::map<std::string, std::vector<int>> grouped;
        for (size_t i = 0; i < sprite_names.size(); ++i) {
            std::string anim_name = get_animation_name(sprite_names[i]);
            if (anim_name.empty()) {
                continue;
            }
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
    std::vector<AnimationItem> normalized_animation_items = animation_items;
    for (AnimationItem& item : normalized_animation_items) {
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

    std::map<std::string, std::string> global_vars;
    if (!layout.atlases.empty()) {
        global_vars["atlas_width"] = std::to_string(layout.atlases[0].width);
        global_vars["atlas_height"] = std::to_string(layout.atlases[0].height);
    } else {
        global_vars["atlas_width"] = "0";
        global_vars["atlas_height"] = "0";
    }
    global_vars["atlas_count"] = std::to_string(layout.atlases.size());
    global_vars["multipack"] = layout.multipack ? "true" : "false";
    global_vars["output_pattern"] = output_pattern_arg;
    
    std::ostringstream atlases_oss;
    if (placeholder_encoding == PlaceholderEncoding::json) {
        atlases_oss << "[";
        for (size_t i = 0; i < layout.atlases.size(); ++i) {
            if (i > 0) atlases_oss << ",";
            std::string a_path = format_atlas_path(output_pattern_arg, static_cast<int>(i));
            atlases_oss << "{\"width\":" << layout.atlases[i].width << ",\"height\":" << layout.atlases[i].height;
            if (!a_path.empty()) {
                atlases_oss << ",\"path\":\"" << escape_json(a_path) << "\"";
            }
            atlases_oss << "}";
        }
        atlases_oss << "]";
    } else if (placeholder_encoding == PlaceholderEncoding::xml) {
        for (size_t i = 0; i < layout.atlases.size(); ++i) {
            std::string a_path = format_atlas_path(output_pattern_arg, static_cast<int>(i));
            atlases_oss << "<atlas index=\"" << i << "\" width=\"" << layout.atlases[i].width << "\" height=\"" << layout.atlases[i].height << "\"";
            if (!a_path.empty()) {
                atlases_oss << " path=\"" << escape_xml(a_path) << "\"";
            }
            atlases_oss << " />\n";
        }
    }
    global_vars["atlases"] = atlases_oss.str();
    global_vars["scale"] = format_double(layout.scale);
    global_vars["extrude"] = std::to_string(layout.extrude);
    global_vars["sprite_count"] = std::to_string(layout.sprites.size());
    global_vars["marker_count"] = std::to_string(marker_items.size());
    global_vars["animation_count"] = std::to_string(normalized_animation_items.size());
    global_vars["fps"] = std::to_string(animation_fps > 0 ? animation_fps : DEFAULT_ANIMATION_FPS);
    global_vars["animation_fps"] = global_vars["fps"];
    global_vars["markers_path"] = markers_path_arg;
    global_vars["animations_path"] = animations_path_arg;
    global_vars["has_markers"] = marker_items.empty() ? "false" : "true";
    global_vars["has_animations"] = normalized_animation_items.empty() ? "false" : "true";
    global_vars["markers_raw"] = markers_text;
    global_vars["animations_raw"] = animations_text;
    if (!transform.header.empty()) {
    std::cout << replace_tokens(transform.header, global_vars, placeholder_encoding);
    }

    auto populate_marker_vars = [&](std::map<std::string, std::string>& vars, const MarkerItem& marker, size_t index) {
        vars["marker_index"] = std::to_string(index);
        vars["marker_name"] = marker.name;
        vars["marker_type"] = marker.type;
        vars["marker_x"] = std::to_string(marker.x);
        vars["marker_y"] = std::to_string(marker.y);
        vars["marker_radius"] = std::to_string(marker.radius);
        vars["marker_w"] = std::to_string(marker.w);
        vars["marker_h"] = std::to_string(marker.h);
        vars["marker_vertices"] = format_vertices(marker.vertices, placeholder_encoding);
        vars["marker_sprite_index"] = std::to_string(marker.sprite_index);
        vars["marker_sprite_name"] = marker.sprite_name;
        vars["marker_sprite_path"] = marker.sprite_path;
    };

    auto populate_sprite_vars = [&](std::map<std::string, std::string>& vars, size_t i) {
        const Sprite& s = layout.sprites[i];
        vars["index"] = std::to_string(i);
        vars["atlas_index"] = std::to_string(s.atlas_index);
        std::string a_path = format_atlas_path(output_pattern_arg, s.atlas_index);
        vars["atlas_path"] = a_path;
        if (s.atlas_index >= 0 && static_cast<size_t>(s.atlas_index) < layout.atlases.size()) {
            vars["atlas_width"] = std::to_string(layout.atlases[static_cast<size_t>(s.atlas_index)].width);
            vars["atlas_height"] = std::to_string(layout.atlases[static_cast<size_t>(s.atlas_index)].height);
        }
        vars["path"] = s.path;
        vars["name"] = sprite_names[i];
        vars["x"] = std::to_string(s.x);
        vars["y"] = std::to_string(s.y);
        vars["w"] = std::to_string(s.w);
        vars["h"] = std::to_string(s.h);
        vars["pivot_x"] = has_global_pivot ? std::to_string(global_pivot_x) : "0";
        vars["pivot_y"] = has_global_pivot ? std::to_string(global_pivot_y) : "0";
        for (const auto& marker : sprite_markers[i]) {
            if (marker.name == "pivot" && marker.type == "point") {
                vars["pivot_x"] = std::to_string(marker.x);
                vars["pivot_y"] = std::to_string(marker.y);
                break;
            }
        }
        vars["src_x"] = std::to_string(s.src_x);
        vars["src_y"] = std::to_string(s.src_y);
        vars["trim_left"] = std::to_string(s.src_x);
        vars["trim_top"] = std::to_string(s.src_y);
        vars["trim_right"] = std::to_string(s.trim_right);
        vars["trim_bottom"] = std::to_string(s.trim_bottom);
        const bool has_trim = (s.src_x != 0) || (s.src_y != 0) || (s.trim_right != 0) || (s.trim_bottom != 0);
        vars["has_trim"] = has_trim ? "true" : "false";
        vars["sprite_markers_count"] = std::to_string(sprite_markers[i].size());
        vars["markers_json"] = format_markers_json(sprite_markers[i]); // Shortcut for quick JSON inclusion

        if (!transform.sprite_marker.empty()) {
            std::string sprite_markers_formatted;
            if (!sprite_markers[i].empty()) {
                if (!transform.sprite_markers_header.empty()) {
                    sprite_markers_formatted += replace_tokens(transform.sprite_markers_header, vars, placeholder_encoding);
                }
                for (size_t j = 0; j < sprite_markers[i].size(); ++j) {
                    if (j > 0 && !transform.sprite_markers_separator.empty()) {
                        sprite_markers_formatted += replace_tokens(transform.sprite_markers_separator, vars, placeholder_encoding);
                    }
                    std::map<std::string, std::string> mvars = vars;
                    populate_marker_vars(mvars, sprite_markers[i][j], j);
                    sprite_markers_formatted += replace_tokens(transform.sprite_marker, mvars, placeholder_encoding);
                }
                if (!transform.sprite_markers_footer.empty()) {
                    sprite_markers_formatted += replace_tokens(transform.sprite_markers_footer, vars, placeholder_encoding);
                }
            }
            vars["sprite_markers"] = sprite_markers_formatted;
        }

        vars["rotation"] = s.rotated ? "90" : "0";
        vars["rotated"] = s.rotated ? "true" : "false";
    };

    if (!marker_items.empty()) {
        if (!transform.if_markers.empty()) {
            std::cout << replace_tokens(transform.if_markers, global_vars, placeholder_encoding);
        }
        if (!transform.markers_header.empty()) {
            std::cout << replace_tokens(transform.markers_header, global_vars, placeholder_encoding);
        }
        if (!transform.markers.empty()) {
            for (size_t i = 0; i < marker_items.size(); ++i) {
                if (i > 0 && !transform.markers_separator.empty()) {
                    std::cout << replace_tokens(transform.markers_separator, global_vars, placeholder_encoding);
                }
                std::map<std::string, std::string> vars = global_vars;
                populate_marker_vars(vars, marker_items[i], i);
                std::cout << replace_tokens(transform.markers, vars, placeholder_encoding);
            }
        }
        if (!transform.markers_footer.empty()) {
            std::cout << replace_tokens(transform.markers_footer, global_vars, placeholder_encoding);
        }
    } else if (!transform.if_no_markers.empty()) {
        std::cout << replace_tokens(transform.if_no_markers, global_vars, placeholder_encoding);
    }

    if (!transform.atlas.empty()) {
        if (!transform.atlas_header.empty()) {
            std::cout << replace_tokens(transform.atlas_header, global_vars, placeholder_encoding);
        }
        for (size_t i = 0; i < layout.atlases.size(); ++i) {
            if (i > 0 && !transform.atlas_separator.empty()) {
                std::cout << replace_tokens(transform.atlas_separator, global_vars, placeholder_encoding);
            }
            std::map<std::string, std::string> avars = global_vars;
            avars["atlas_index"] = std::to_string(i);
            avars["atlas_width"] = std::to_string(layout.atlases[i].width);
            avars["atlas_height"] = std::to_string(layout.atlases[i].height);
            std::string a_path = format_atlas_path(output_pattern_arg, static_cast<int>(i));
            avars["atlas_path"] = a_path;
            avars["atlas_path_json"] = escape_json(a_path);
            avars["atlas_path_xml"] = escape_xml(a_path);
            avars["atlas_path_csv"] = escape_csv(a_path);
            avars["atlas_path_css"] = escape_css_string(a_path);

            std::string sprites_in_atlas;
            for (size_t j = 0; j < layout.sprites.size(); ++j) {
                if (layout.sprites[j].atlas_index == (int)i) {
                    if (!sprites_in_atlas.empty() && !transform.separator.empty()) {
                        sprites_in_atlas += replace_tokens(transform.separator, avars, placeholder_encoding);
                    }
                    std::map<std::string, std::string> svars = avars;
                    populate_sprite_vars(svars, j);
                    sprites_in_atlas += replace_tokens(transform.sprite, svars, placeholder_encoding);
                }
            }
            avars["sprites"] = sprites_in_atlas;
            std::cout << replace_tokens(transform.atlas, avars, placeholder_encoding);
        }
        if (!transform.atlas_footer.empty()) {
            std::cout << replace_tokens(transform.atlas_footer, global_vars, placeholder_encoding);
        }
    } else {
        for (size_t i = 0; i < layout.sprites.size(); ++i) {
            if (i > 0 && !transform.separator.empty()) {
                std::cout << replace_tokens(transform.separator, global_vars, placeholder_encoding);
            }
            std::map<std::string, std::string> vars = global_vars;
            populate_sprite_vars(vars, i);
            std::cout << replace_tokens(transform.sprite, vars, placeholder_encoding);
        }
    }

    if (!normalized_animation_items.empty()) {
        if (!transform.if_animations.empty()) {
            std::cout << replace_tokens(transform.if_animations, global_vars, placeholder_encoding);
        }
        if (!transform.animations_header.empty()) {
            std::cout << replace_tokens(transform.animations_header, global_vars, placeholder_encoding);
        }
        if (!transform.animations.empty()) {
            for (size_t i = 0; i < normalized_animation_items.size(); ++i) {
                if (i > 0 && !transform.animations_separator.empty()) {
                    std::cout << replace_tokens(transform.animations_separator, global_vars, placeholder_encoding);
                }
                const AnimationItem& animation = normalized_animation_items[i];
                std::map<std::string, std::string> vars = global_vars;
                vars["animation_index"] = std::to_string(i);
                vars["animation_name"] = animation.name;
                vars["animation_sprite_count"] = std::to_string(animation.sprite_indexes.size());
                vars["sprite_indexes"] = format_sprite_indexes(animation.sprite_indexes, placeholder_encoding);
                vars["fps"] = std::to_string(animation.fps);
                vars["animation_fps"] = vars["fps"];
        std::cout << replace_tokens(transform.animations, vars, placeholder_encoding);
            }
        }
        if (!transform.animations_footer.empty()) {
            std::cout << replace_tokens(transform.animations_footer, global_vars, placeholder_encoding);
        }
    } else if (!transform.if_no_animations.empty()) {
        std::cout << replace_tokens(transform.if_no_animations, global_vars, placeholder_encoding);
    }

    if (!transform.footer.empty()) {
        std::cout << replace_tokens(transform.footer, global_vars, placeholder_encoding);
    }

    return 0;
}
