#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

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

struct Layout {
    int atlas_width = 0;
    int atlas_height = 0;
    double scale = 1.0;
    bool has_scale = false;
    std::vector<Sprite> sprites;
};

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
    std::string separator;
    std::string if_animations;
    std::string if_no_animations;
    std::string animations_header;
    std::string animations;
    std::string animations_separator;
    std::string animations_footer;
    std::string footer;
};

struct MarkerItem {
    size_t index = 0;
    int sprite_index = -1;
    std::string sprite_name;
    std::string sprite_path;
    std::string name;
};

struct AnimationItem {
    size_t index = 0;
    std::string name;
    std::vector<int> sprite_indexes;
};

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
    out = value;
    return std::isfinite(value);
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

bool parse_layout(std::istream& in, Layout& out, std::string& error) {
    Layout parsed;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (line.rfind("atlas", 0) == 0) {
            if (!parse_atlas_line(line, parsed.atlas_width, parsed.atlas_height)) {
                error = "Invalid atlas line: " + line;
                return false;
            }
        } else if (line.rfind("scale", 0) == 0) {
            if (parsed.has_scale) {
                error = "Duplicate scale line";
                return false;
            }
            if (!parse_scale_line(line, parsed.scale)) {
                error = "Invalid scale line: " + line;
                return false;
            }
            parsed.has_scale = true;
        } else if (line.rfind("sprite", 0) == 0) {
            Sprite s;
            std::string sprite_error;
            if (!parse_sprite_line(line, s, sprite_error)) {
                error = "Invalid sprite line: " + sprite_error;
                return false;
            }
            parsed.sprites.push_back(s);
        } else {
            error = "Unknown line: " + line;
            return false;
        }
    }

    if (parsed.atlas_width <= 0 || parsed.atlas_height <= 0) {
        error = "Invalid atlas size";
        return false;
    }

    if (!parsed.has_scale) {
        parsed.scale = 1.0;
    }

    out = std::move(parsed);
    return true;
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
    out.reserve(s.size() + 8);
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
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u";
                    const char* digits = "0123456789abcdef";
                    unsigned char uc = static_cast<unsigned char>(c);
                    hex << '0' << '0' << digits[(uc >> 4) & 0x0f] << digits[uc & 0x0f];
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
    out.reserve(s.size() + 8);
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
    out.reserve(s.size() + 8);
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

std::string replace_tokens(const std::string& input, const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(input.size() + 64);

    size_t i = 0;
    while (i < input.size()) {
        size_t open = input.find("{{", i);
        if (open == std::string::npos) {
            out.append(input.substr(i));
            break;
        }

        out.append(input.substr(i, open - i));
        size_t close = input.find("}}", open + 2);
        if (close == std::string::npos) {
            out.append(input.substr(open));
            break;
        }

        std::string key = trim_copy(input.substr(open + 2, close - (open + 2)));
        auto it = vars.find(key);
        if (it != vars.end()) {
            out.append(it->second);
        }
        i = close + 2;
    }

    return out;
}

size_t skip_json_ws(const std::string& s, size_t pos) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])) != 0) {
        ++pos;
    }
    return pos;
}

size_t scan_json_string(const std::string& s, size_t pos) {
    if (pos >= s.size() || s[pos] != '"') {
        return std::string::npos;
    }
    ++pos;
    while (pos < s.size()) {
        if (s[pos] == '\\') {
            pos += 2;
            continue;
        }
        if (s[pos] == '"') {
            return pos + 1;
        }
        ++pos;
    }
    return std::string::npos;
}

size_t find_matching_json_bracket(const std::string& s, size_t open_pos, char open_ch, char close_ch) {
    if (open_pos >= s.size() || s[open_pos] != open_ch) {
        return std::string::npos;
    }
    int depth = 0;
    size_t pos = open_pos;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') {
            size_t end = scan_json_string(s, pos);
            if (end == std::string::npos) {
                return std::string::npos;
            }
            pos = end;
            continue;
        }
        if (c == open_ch) {
            ++depth;
        } else if (c == close_ch) {
            --depth;
            if (depth == 0) {
                return pos;
            }
        }
        ++pos;
    }
    return std::string::npos;
}

bool parse_json_array_items(const std::string& text, std::vector<std::string>& out_items) {
    out_items.clear();
    size_t start = skip_json_ws(text, 0);
    if (start >= text.size() || text[start] != '[') {
        return false;
    }

    size_t end = find_matching_json_bracket(text, start, '[', ']');
    if (end == std::string::npos) {
        return false;
    }
    if (skip_json_ws(text, end + 1) != text.size()) {
        return false;
    }

    size_t item_start = start + 1;
    int object_depth = 0;
    int array_depth = 0;
    size_t pos = item_start;
    while (pos < end) {
        char c = text[pos];
        if (c == '"') {
            size_t str_end = scan_json_string(text, pos);
            if (str_end == std::string::npos) {
                return false;
            }
            pos = str_end;
            continue;
        }
        if (c == '{') {
            ++object_depth;
        } else if (c == '}') {
            if (object_depth <= 0) {
                return false;
            }
            --object_depth;
        } else if (c == '[') {
            ++array_depth;
        } else if (c == ']') {
            if (array_depth <= 0) {
                return false;
            }
            --array_depth;
        } else if (c == ',' && object_depth == 0 && array_depth == 0) {
            std::string item = trim_copy(text.substr(item_start, pos - item_start));
            if (!item.empty()) {
                out_items.push_back(item);
            }
            item_start = pos + 1;
        }
        ++pos;
    }

    std::string tail = trim_copy(text.substr(item_start, end - item_start));
    if (!tail.empty()) {
        out_items.push_back(tail);
    }
    return true;
}

bool extract_first_object_array(const std::string& text, std::string& out_array_text) {
    size_t start = skip_json_ws(text, 0);
    if (start >= text.size() || text[start] != '{') {
        return false;
    }
    size_t end = find_matching_json_bracket(text, start, '{', '}');
    if (end == std::string::npos) {
        return false;
    }
    if (skip_json_ws(text, end + 1) != text.size()) {
        return false;
    }

    int object_depth = 0;
    int array_depth = 0;
    size_t pos = start;
    while (pos <= end) {
        char c = text[pos];
        if (c == '"') {
            size_t str_end = scan_json_string(text, pos);
            if (str_end == std::string::npos) {
                return false;
            }
            pos = str_end;
            continue;
        }
        if (c == '{') {
            ++object_depth;
        } else if (c == '}') {
            --object_depth;
        } else if (c == '[') {
            if (object_depth == 1 && array_depth == 0) {
                size_t arr_end = find_matching_json_bracket(text, pos, '[', ']');
                if (arr_end == std::string::npos || arr_end > end) {
                    return false;
                }
                out_array_text = text.substr(pos, arr_end - pos + 1);
                return true;
            }
            ++array_depth;
        } else if (c == ']') {
            --array_depth;
        }
        ++pos;
    }
    return false;
}

bool parse_json_string_literal(const std::string& token, std::string& out) {
    if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
        return false;
    }
    out.clear();
    out.reserve(token.size() - 2);
    for (size_t i = 1; i + 1 < token.size(); ++i) {
        char c = token[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= token.size() - 1) {
            return false;
        }
        char e = token[++i];
        switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                // Keep unicode escapes as-is for now.
                out += "\\u";
                if (i + 4 >= token.size() - 1) {
                    return false;
                }
                out.push_back(token[++i]);
                out.push_back(token[++i]);
                out.push_back(token[++i]);
                out.push_back(token[++i]);
                break;
            default:
                out.push_back(e);
                break;
        }
    }
    return true;
}

bool parse_json_object_entries(const std::string& text, std::vector<std::pair<std::string, std::string>>& entries) {
    entries.clear();
    size_t start = skip_json_ws(text, 0);
    if (start >= text.size() || text[start] != '{') {
        return false;
    }
    size_t end = find_matching_json_bracket(text, start, '{', '}');
    if (end == std::string::npos) {
        return false;
    }
    if (skip_json_ws(text, end + 1) != text.size()) {
        return false;
    }

    size_t pos = skip_json_ws(text, start + 1);
    while (pos < end) {
        if (text[pos] != '"') {
            return false;
        }
        size_t key_end = scan_json_string(text, pos);
        if (key_end == std::string::npos) {
            return false;
        }

        std::string key_token = text.substr(pos, key_end - pos);
        std::string key;
        if (!parse_json_string_literal(key_token, key)) {
            return false;
        }
        pos = skip_json_ws(text, key_end);
        if (pos >= end || text[pos] != ':') {
            return false;
        }
        ++pos;
        pos = skip_json_ws(text, pos);
        if (pos >= end) {
            return false;
        }

        size_t value_start = pos;
        if (text[pos] == '"') {
            size_t value_end = scan_json_string(text, pos);
            if (value_end == std::string::npos) {
                return false;
            }
            pos = value_end;
        } else if (text[pos] == '{') {
            size_t value_end = find_matching_json_bracket(text, pos, '{', '}');
            if (value_end == std::string::npos) {
                return false;
            }
            pos = value_end + 1;
        } else if (text[pos] == '[') {
            size_t value_end = find_matching_json_bracket(text, pos, '[', ']');
            if (value_end == std::string::npos) {
                return false;
            }
            pos = value_end + 1;
        } else {
            while (pos < end && text[pos] != ',' && text[pos] != '}') {
                ++pos;
            }
        }
        std::string value = trim_copy(text.substr(value_start, pos - value_start));
        entries.push_back({key, value});

        pos = skip_json_ws(text, pos);
        if (pos < end && text[pos] == ',') {
            ++pos;
            pos = skip_json_ws(text, pos);
        }
    }

    return true;
}

bool json_entry_value(const std::vector<std::pair<std::string, std::string>>& entries,
                      const std::string& key,
                      std::string& out_value) {
    for (const auto& entry : entries) {
        if (entry.first == key) {
            out_value = entry.second;
            return true;
        }
    }
    return false;
}

bool parse_json_int_literal(const std::string& token, int& out) {
    return parse_int(trim_copy(token), out);
}

std::string join_ints_csv(const std::vector<int>& values, const std::string& sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << sep;
        }
        oss << values[i];
    }
    return oss.str();
}

std::string ints_to_json_array(const std::vector<int>& values) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << values[i];
    }
    oss << "]";
    return oss.str();
}

std::string strings_to_json_array(const std::vector<std::string>& values) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "\"" << escape_json(values[i]) << "\"";
    }
    oss << "]";
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
    for (size_t i = 0; i < layout.sprites.size(); ++i) {
        const Sprite& s = layout.sprites[i];
        const int idx = static_cast<int>(i);
        by_path[s.path] = idx;
        fs::path p(s.path);
        by_path[p.filename().string()] = idx;
        std::string name = sprite_name_from_path(s.path);
        sprite_names.push_back(name);
        by_name[name] = idx;
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
                                           std::vector<std::vector<std::string>>& sprite_markers) {
    sprite_markers.assign(layout.sprites.size(), {});
    std::vector<MarkerItem> markers;
    const std::string trimmed = trim_copy(markers_text);
    if (trimmed.empty()) {
        return markers;
    }

    std::vector<std::pair<std::string, std::string>> root_entries;
    if (!parse_json_object_entries(trimmed, root_entries)) {
        return markers;
    }

    std::string sprites_obj = "";
    std::string spritemarkers_obj;
    if (json_entry_value(root_entries, "spritemarkers", spritemarkers_obj)) {
        std::vector<std::pair<std::string, std::string>> sm_entries;
        if (parse_json_object_entries(spritemarkers_obj, sm_entries)) {
            json_entry_value(sm_entries, "sprites", sprites_obj);
        }
    }
    if (sprites_obj.empty()) {
        json_entry_value(root_entries, "sprites", sprites_obj);
    }
    if (sprites_obj.empty()) {
        return markers;
    }

    std::vector<std::pair<std::string, std::string>> sprite_entries;
    if (!parse_json_object_entries(sprites_obj, sprite_entries)) {
        return markers;
    }

    for (const auto& sprite_entry : sprite_entries) {
        const std::string& sprite_key = sprite_entry.first;
        const std::string& sprite_value = sprite_entry.second;
        int sprite_index = resolve_sprite_index(sprite_key, by_path, by_name);

        std::vector<std::pair<std::string, std::string>> sprite_obj_entries;
        std::string markers_array;
        if (parse_json_object_entries(sprite_value, sprite_obj_entries)) {
            std::string explicit_name;
            if (sprite_index < 0 && json_entry_value(sprite_obj_entries, "name", explicit_name)) {
                std::string decoded_name;
                if (parse_json_string_literal(explicit_name, decoded_name)) {
                    sprite_index = resolve_sprite_index(decoded_name, by_path, by_name);
                }
            }
            json_entry_value(sprite_obj_entries, "markers", markers_array);
        }
        if (sprite_index < 0 || markers_array.empty()) {
            continue;
        }

        std::vector<std::string> marker_values;
        if (!parse_json_array_items(markers_array, marker_values)) {
            continue;
        }
        for (const std::string& marker_value : marker_values) {
            std::string marker_name;
            std::string marker_trimmed = trim_copy(marker_value);
            if (marker_trimmed.empty()) {
                continue;
            }
            if (marker_trimmed.front() == '"') {
                if (!parse_json_string_literal(marker_trimmed, marker_name)) {
                    continue;
                }
            } else {
                std::vector<std::pair<std::string, std::string>> marker_obj_entries;
                if (!parse_json_object_entries(marker_trimmed, marker_obj_entries)) {
                    continue;
                }
                std::string marker_name_value;
                if (!json_entry_value(marker_obj_entries, "name", marker_name_value)) {
                    continue;
                }
                if (!parse_json_string_literal(marker_name_value, marker_name)) {
                    continue;
                }
            }

            if (marker_name.empty()) {
                continue;
            }
            sprite_markers[static_cast<size_t>(sprite_index)].push_back(marker_name);
            MarkerItem item;
            item.index = markers.size();
            item.sprite_index = sprite_index;
            item.sprite_name = sprite_names[static_cast<size_t>(sprite_index)];
            item.sprite_path = layout.sprites[static_cast<size_t>(sprite_index)].path;
            item.name = marker_name;
            markers.push_back(std::move(item));
        }
    }

    return markers;
}

std::vector<AnimationItem> parse_animations_data(const std::string& animations_text,
                                                 const std::unordered_map<std::string, int>& by_path,
                                                 const std::unordered_map<std::string, int>& by_name) {
    std::vector<AnimationItem> animations;
    const std::string trimmed = trim_copy(animations_text);
    if (trimmed.empty()) {
        return animations;
    }

    std::string timelines_array = trimmed;
    if (trimmed.front() == '{') {
        std::vector<std::pair<std::string, std::string>> root_entries;
        if (!parse_json_object_entries(trimmed, root_entries)) {
            return animations;
        }
        if (!json_entry_value(root_entries, "timelines", timelines_array)) {
            if (!json_entry_value(root_entries, "animations", timelines_array)) {
                return animations;
            }
        }
    }

    std::vector<std::string> timeline_values;
    if (!parse_json_array_items(timelines_array, timeline_values)) {
        return animations;
    }

    for (const std::string& timeline : timeline_values) {
        std::vector<std::pair<std::string, std::string>> timeline_entries;
        if (!parse_json_object_entries(trim_copy(timeline), timeline_entries)) {
            continue;
        }

        std::string name_value;
        std::string name = "animation_" + std::to_string(animations.size());
        if (json_entry_value(timeline_entries, "name", name_value)) {
            std::string decoded_name;
            if (parse_json_string_literal(name_value, decoded_name) && !decoded_name.empty()) {
                name = decoded_name;
            }
        }

        std::vector<int> indexes;
        std::string indexes_array;
        if (json_entry_value(timeline_entries, "sprite_indexes", indexes_array)) {
            std::vector<std::string> tokens;
            if (parse_json_array_items(indexes_array, tokens)) {
                for (const std::string& token : tokens) {
                    int idx = -1;
                    if (parse_json_int_literal(token, idx)) {
                        indexes.push_back(idx);
                    }
                }
            }
        } else if (json_entry_value(timeline_entries, "frames", indexes_array)) {
            std::vector<std::string> frame_tokens;
            if (parse_json_array_items(indexes_array, frame_tokens)) {
                for (const std::string& frame_token : frame_tokens) {
                    std::string token = trim_copy(frame_token);
                    int idx = -1;
                    if (parse_json_int_literal(token, idx)) {
                        indexes.push_back(idx);
                        continue;
                    }
                    std::string path_or_name;
                    if (parse_json_string_literal(token, path_or_name)) {
                        idx = resolve_sprite_index(path_or_name, by_path, by_name);
                        if (idx >= 0) {
                            indexes.push_back(idx);
                        }
                    }
                }
            }
        }

        AnimationItem item;
        item.index = animations.size();
        item.name = name;
        item.sprite_indexes = std::move(indexes);
        animations.push_back(std::move(item));
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

    auto append_line = [](std::string& target, const std::string& value) {
        if (!target.empty()) {
            target.push_back('\n');
        }
        target.append(value);
    };

    auto is_known_section = [](const std::string& s) {
        return s == "meta" ||
               s == "header" ||
               s == "if_markers" ||
               s == "if_no_markers" ||
               s == "markers_header" ||
               s == "markers" ||
               s == "marker" ||
               s == "markers_separator" ||
               s == "markers_footer" ||
               s == "sprites" ||
               s == "sprite" ||
               s == "separator" ||
               s == "if_animations" ||
               s == "if_no_animations" ||
               s == "animations_header" ||
               s == "animations" ||
               s == "animation" ||
               s == "animations_separator" ||
               s == "animations_footer" ||
               s == "footer";
    };

    while (std::getline(in, line)) {
        if (line.empty() && section_stack.empty()) {
            continue;
        }

        std::string trimmed = trim_copy(line);
        if (trimmed.empty() && section_stack.empty()) {
            continue;
        }

        if (!trimmed.empty() && trimmed.front() == '#') {
            continue;
        }

        if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string tag = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            if (!tag.empty() && tag.front() == '/') {
                std::string closing = trim_copy(tag.substr(1));
                if (section_stack.empty()) {
                    error = "Unexpected closing section [/" + closing + "]: " + path.string();
                    return false;
                }
                if (closing != section_stack.back()) {
                    error = "Mismatched closing section [/" + closing + "] for [" + section_stack.back() + "]: " + path.string();
                    return false;
                }
                section_stack.pop_back();
                continue;
            }

            if (!is_known_section(tag)) {
                error = "Unknown section [" + tag + "]: " + path.string();
                return false;
            }
            if (tag == "sprite") {
                if (section_stack.empty() || section_stack.back() != "sprites") {
                    error = "Section [sprite] must be inside [sprites]: " + path.string();
                    return false;
                }
                saw_sprite_item = true;
            } else if (tag == "marker") {
                if (section_stack.empty() || section_stack.back() != "markers") {
                    error = "Section [marker] must be inside [markers]: " + path.string();
                    return false;
                }
                saw_marker_item = true;
            } else if (tag == "animation") {
                if (section_stack.empty() || section_stack.back() != "animations") {
                    error = "Section [animation] must be inside [animations]: " + path.string();
                    return false;
                }
                saw_animation_item = true;
            }
            section_stack.push_back(tag);
            continue;
        }

        const std::string section = section_stack.empty() ? "" : section_stack.back();

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
        } else if (section == "footer") {
            append_line(parsed.footer, line);
        }
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
    oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
    oss.precision(8);
    oss << value;
    return oss.str();
}

fs::path find_transforms_dir(const std::string& argv0) {
    std::vector<fs::path> candidates;
    candidates.push_back(fs::path("transforms"));
#ifdef SPRAT_SOURCE_DIR
    candidates.push_back(fs::path(SPRAT_SOURCE_DIR) / "transforms");
#endif
    if (!argv0.empty()) {
        fs::path exe_dir = fs::path(argv0).parent_path();
        if (!exe_dir.empty()) {
            candidates.push_back(exe_dir / "transforms");
            candidates.push_back(exe_dir / ".." / "transforms");
            candidates.push_back(exe_dir / ".." / ".." / "transforms");
        }
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return fs::path("transforms");
}

fs::path resolve_transform_path(const std::string& transform_arg, const std::string& argv0) {
    fs::path candidate(transform_arg);
    if (candidate.has_parent_path() || candidate.extension() == ".transform") {
        return candidate;
    }
    return find_transforms_dir(argv0) / (transform_arg + ".transform");
}

bool load_transform_by_name(const std::string& transform_arg, const std::string& argv0, Transform& out, std::string& error) {
    fs::path transform_path = resolve_transform_path(transform_arg, argv0);
    return parse_transform_file(transform_path, out, error);
}

void list_transforms(const std::string& argv0) {
    const fs::path dir = find_transforms_dir(argv0);
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
    std::sort(paths.begin(), paths.end());

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

void print_usage() {
    std::cerr << "Usage: spratconvert [--transform NAME|PATH] [--markers FILE] [--animations FILE] [--list-transforms]\n";
}

int main(int argc, char** argv) {
    std::string transform_arg = "json";
    std::string markers_path_arg;
    std::string animations_path_arg;
    bool list_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--transform" && i + 1 < argc) {
            transform_arg = argv[++i];
        } else if (arg == "--markers" && i + 1 < argc) {
            markers_path_arg = argv[++i];
        } else if (arg == "--animations" && i + 1 < argc) {
            animations_path_arg = argv[++i];
        } else if (arg == "--list-transforms") {
            list_only = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            print_usage();
            return 1;
        }
    }

    if (list_only) {
        list_transforms(argv[0] ? argv[0] : "");
        return 0;
    }

    Transform transform;
    std::string transform_error;
    if (!load_transform_by_name(transform_arg, argv[0] ? argv[0] : "", transform, transform_error)) {
        std::cerr << transform_error << "\n";
        return 1;
    }

    std::string markers_text;
    std::string animations_text;
    if (!markers_path_arg.empty()) {
        std::string file_error;
        if (!read_text_file(fs::path(markers_path_arg), markers_text, file_error)) {
            std::cerr << file_error << "\n";
            return 1;
        }
    }
    if (!animations_path_arg.empty()) {
        std::string file_error;
        if (!read_text_file(fs::path(animations_path_arg), animations_text, file_error)) {
            std::cerr << file_error << "\n";
            return 1;
        }
    }
    Layout layout;
    std::string layout_error;
    if (!parse_layout(std::cin, layout, layout_error)) {
        std::cerr << layout_error << "\n";
        return 1;
    }

    std::unordered_map<std::string, int> sprite_index_by_path;
    std::unordered_map<std::string, int> sprite_index_by_name;
    std::vector<std::string> sprite_names;
    collect_sprite_name_indexes(layout, sprite_index_by_path, sprite_index_by_name, sprite_names);

    std::vector<std::vector<std::string>> sprite_markers;
    const std::vector<MarkerItem> marker_items =
        parse_markers_data(markers_text, layout, sprite_index_by_path, sprite_index_by_name, sprite_names, sprite_markers);
    const std::vector<AnimationItem> animation_items =
        parse_animations_data(animations_text, sprite_index_by_path, sprite_index_by_name);
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

    std::map<std::string, std::string> global_vars;
    global_vars["atlas_width"] = std::to_string(layout.atlas_width);
    global_vars["atlas_height"] = std::to_string(layout.atlas_height);
    global_vars["scale"] = format_double(layout.scale);
    global_vars["sprite_count"] = std::to_string(layout.sprites.size());
    global_vars["marker_count"] = std::to_string(marker_items.size());
    global_vars["animation_count"] = std::to_string(normalized_animation_items.size());
    global_vars["markers_path"] = markers_path_arg;
    global_vars["animations_path"] = animations_path_arg;
    global_vars["has_markers"] = marker_items.empty() ? "false" : "true";
    global_vars["has_animations"] = normalized_animation_items.empty() ? "false" : "true";
    global_vars["markers_raw"] = markers_text;
    global_vars["markers_json"] = escape_json(markers_text);
    global_vars["markers_xml"] = escape_xml(markers_text);
    global_vars["markers_csv"] = escape_csv(markers_text);
    global_vars["markers_css"] = escape_css_string(markers_text);
    global_vars["animations_raw"] = animations_text;
    global_vars["animations_json"] = escape_json(animations_text);
    global_vars["animations_xml"] = escape_xml(animations_text);
    global_vars["animations_csv"] = escape_csv(animations_text);
    global_vars["animations_css"] = escape_css_string(animations_text);

    if (!transform.header.empty()) {
        std::cout << replace_tokens(transform.header, global_vars);
    }

    if (!marker_items.empty()) {
        if (!transform.if_markers.empty()) {
            std::cout << replace_tokens(transform.if_markers, global_vars);
        }
        if (!transform.markers_header.empty()) {
            std::cout << replace_tokens(transform.markers_header, global_vars);
        }
        if (!transform.markers.empty()) {
            for (size_t i = 0; i < marker_items.size(); ++i) {
                if (i > 0 && !transform.markers_separator.empty()) {
                    std::cout << replace_tokens(transform.markers_separator, global_vars);
                }
                const MarkerItem& marker = marker_items[i];
                std::map<std::string, std::string> vars = global_vars;
                vars["marker_index"] = std::to_string(i);
                vars["marker_name"] = marker.name;
                vars["marker_name_json"] = escape_json(marker.name);
                vars["marker_name_xml"] = escape_xml(marker.name);
                vars["marker_name_csv"] = escape_csv(marker.name);
                vars["marker_name_css"] = escape_css_string(marker.name);
                vars["marker_sprite_index"] = std::to_string(marker.sprite_index);
                vars["marker_sprite_name"] = marker.sprite_name;
                vars["marker_sprite_path"] = marker.sprite_path;
                vars["marker_sprite_name_json"] = escape_json(marker.sprite_name);
                vars["marker_sprite_name_xml"] = escape_xml(marker.sprite_name);
                vars["marker_sprite_name_csv"] = escape_csv(marker.sprite_name);
                vars["marker_sprite_name_css"] = escape_css_string(marker.sprite_name);
                vars["marker_sprite_path_json"] = escape_json(marker.sprite_path);
                vars["marker_sprite_path_xml"] = escape_xml(marker.sprite_path);
                vars["marker_sprite_path_csv"] = escape_csv(marker.sprite_path);
                vars["marker_sprite_path_css"] = escape_css_string(marker.sprite_path);
                std::cout << replace_tokens(transform.markers, vars);
            }
        }
        if (!transform.markers_footer.empty()) {
            std::cout << replace_tokens(transform.markers_footer, global_vars);
        }
    } else if (!transform.if_no_markers.empty()) {
        std::cout << replace_tokens(transform.if_no_markers, global_vars);
    }

    for (size_t i = 0; i < layout.sprites.size(); ++i) {
        if (i > 0 && !transform.separator.empty()) {
            std::cout << replace_tokens(transform.separator, global_vars);
        }

        const Sprite& s = layout.sprites[i];
        std::map<std::string, std::string> vars = global_vars;
        vars["index"] = std::to_string(i);
        vars["path"] = s.path;
        vars["name"] = sprite_names[i];
        vars["name_json"] = escape_json(sprite_names[i]);
        vars["name_xml"] = escape_xml(sprite_names[i]);
        vars["name_csv"] = escape_csv(sprite_names[i]);
        vars["name_css"] = escape_css_string(sprite_names[i]);
        vars["path_json"] = escape_json(s.path);
        vars["path_xml"] = escape_xml(s.path);
        vars["path_csv"] = escape_csv(s.path);
        vars["path_css"] = escape_css_string(s.path);
        vars["x"] = std::to_string(s.x);
        vars["y"] = std::to_string(s.y);
        vars["w"] = std::to_string(s.w);
        vars["h"] = std::to_string(s.h);
        vars["src_x"] = std::to_string(s.src_x);
        vars["src_y"] = std::to_string(s.src_y);
        vars["trim_right"] = std::to_string(s.trim_right);
        vars["trim_bottom"] = std::to_string(s.trim_bottom);
        vars["has_trim"] = s.has_trim ? "true" : "false";
        vars["sprite_markers_count"] = std::to_string(sprite_markers[i].size());
        vars["sprite_markers_json"] = strings_to_json_array(sprite_markers[i]);
        vars["sprite_markers_xml"] = escape_xml(vars["sprite_markers_json"]);
        vars["sprite_markers_csv"] = escape_csv(vars["sprite_markers_json"]);
        vars["sprite_markers_css"] = escape_css_string(vars["sprite_markers_json"]);

        std::cout << replace_tokens(transform.sprite, vars);
    }

    if (!normalized_animation_items.empty()) {
        if (!transform.if_animations.empty()) {
            std::cout << replace_tokens(transform.if_animations, global_vars);
        }
        if (!transform.animations_header.empty()) {
            std::cout << replace_tokens(transform.animations_header, global_vars);
        }
        if (!transform.animations.empty()) {
            for (size_t i = 0; i < normalized_animation_items.size(); ++i) {
                if (i > 0 && !transform.animations_separator.empty()) {
                    std::cout << replace_tokens(transform.animations_separator, global_vars);
                }
                const AnimationItem& animation = normalized_animation_items[i];
                std::map<std::string, std::string> vars = global_vars;
                vars["animation_index"] = std::to_string(i);
                vars["animation_name"] = animation.name;
                vars["animation_name_json"] = escape_json(animation.name);
                vars["animation_name_xml"] = escape_xml(animation.name);
                vars["animation_name_csv"] = escape_csv(animation.name);
                vars["animation_name_css"] = escape_css_string(animation.name);
                vars["animation_sprite_count"] = std::to_string(animation.sprite_indexes.size());
                vars["animation_sprite_indexes_json"] = ints_to_json_array(animation.sprite_indexes);
                vars["animation_sprite_indexes_csv"] = join_ints_csv(animation.sprite_indexes, "|");
                vars["animation_sprite_indexes"] = join_ints_csv(animation.sprite_indexes, ",");
                vars["animation_sprite_indexes_xml"] = escape_xml(vars["animation_sprite_indexes_json"]);
                vars["animation_sprite_indexes_css"] = escape_css_string(vars["animation_sprite_indexes_json"]);
                std::cout << replace_tokens(transform.animations, vars);
            }
        }
        if (!transform.animations_footer.empty()) {
            std::cout << replace_tokens(transform.animations_footer, global_vars);
        }
    } else if (!transform.if_no_animations.empty()) {
        std::cout << replace_tokens(transform.if_no_animations, global_vars);
    }

    if (!transform.footer.empty()) {
        std::cout << replace_tokens(transform.footer, global_vars);
    }

    return 0;
}
