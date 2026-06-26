#include "layout_parser.h"
#include "cli_parse.h"

#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_set>

static bool parse_slice(const std::string& val, int& left, int& top, int& right, int& bottom,
                        std::string& h_mode, std::string& v_mode) {
    // Expect "L,T,R,B" or "L,T,R,B,H_MODE,V_MODE"
    size_t p1 = val.find(',');
    if (p1 == std::string::npos) return false;
    size_t p2 = val.find(',', p1 + 1);
    if (p2 == std::string::npos) return false;
    size_t p3 = val.find(',', p2 + 1);
    if (p3 == std::string::npos) return false;

    size_t p4 = val.find(',', p3 + 1);
    if (p4 == std::string::npos) {
        // 4 values only
        if (!sprat::core::parse_non_negative_int(val.substr(0, p1), left)
            || !sprat::core::parse_non_negative_int(val.substr(p1 + 1, p2 - p1 - 1), top)
            || !sprat::core::parse_non_negative_int(val.substr(p2 + 1, p3 - p2 - 1), right)
            || !sprat::core::parse_non_negative_int(val.substr(p3 + 1), bottom)) {
            return false;
        }
        h_mode = "stretch";
        v_mode = "stretch";
        return true;
    }

    size_t p5 = val.find(',', p4 + 1);
    if (p5 == std::string::npos) return false;
    // No more commas after p5
    if (val.find(',', p5 + 1) != std::string::npos) return false;

    if (!sprat::core::parse_non_negative_int(val.substr(0, p1), left)
        || !sprat::core::parse_non_negative_int(val.substr(p1 + 1, p2 - p1 - 1), top)
        || !sprat::core::parse_non_negative_int(val.substr(p2 + 1, p3 - p2 - 1), right)
        || !sprat::core::parse_non_negative_int(val.substr(p3 + 1, p4 - p3 - 1), bottom)) {
        return false;
    }

    h_mode = val.substr(p4 + 1, p5 - p4 - 1);
    v_mode = val.substr(p5 + 1);

    if (h_mode != "stretch" && h_mode != "repeat" && h_mode != "mirror") return false;
    if (v_mode != "stretch" && v_mode != "repeat" && v_mode != "mirror") return false;

    return true;
}

// Returns true for line prefixes that appear in the combined raw-layout format
// produced by spratconvert but carry no meaning for the basic layout parser.
// Adding a new prefix here is the only change needed if the format gains a
// new ignored token type.
static bool is_combined_format_passthrough(const std::string& line) {
    constexpr std::array<std::string_view, 5> k_prefixes = {
        "path", "- marker", "- frame", "animation", "fps"
    };
    for (const auto& prefix : k_prefixes) {
        if (line.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

namespace sprat::core {

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
    if (pos >= line.size() || line[pos] != '"') {
        error = "sprite path must be quoted";
        return false;
    }

    if (!parse_quoted(line, pos, path, error)) {
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
        if (token == "rotated") {
            parsed.rotated = true;
        } else if (token == "dither") {
            parsed.dither = true;
        } else if (token.starts_with("colors=")) {
            std::string val = token.substr(7);
            if (!parse_int(val, parsed.colors) || (parsed.colors != 0 && (parsed.colors < 2 || parsed.colors > 256))) {
                error = "invalid colors value (must be 0 or 2-256): " + val;
                return false;
            }
        } else if (token.starts_with("slice=")) {
            std::string val = token.substr(6);
            if (!parse_slice(val, parsed.slice_left, parsed.slice_top,
                             parsed.slice_right, parsed.slice_bottom,
                             parsed.slice_h, parsed.slice_v)) {
                error = "invalid slice value (expected L,T,R,B[,H_MODE,V_MODE] with non-negative integers and optional stretch/repeat/mirror modes): " + val;
                return false;
            }
            parsed.has_slice = true;
        } else {
            tokens.push_back(token);
        }
    }

    if (tokens.empty()) {
        error = "sprite line is missing numeric fields";
        return false;
    }

    if (tokens[0].find(',') != std::string::npos) {
        constexpr size_t MODERN_SPRITE_TOKENS_MIN = 2;
        constexpr size_t MODERN_SPRITE_TOKENS_MAX = 4;
        if (tokens.size() != MODERN_SPRITE_TOKENS_MIN && tokens.size() != MODERN_SPRITE_TOKENS_MAX) {
            error = "sprite line must contain position/size and optional trim offsets";
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y) || !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = "invalid position or size pair";
            return false;
        }
        if (parsed.x < 0 || parsed.y < 0) {
            error = "sprite position must not be negative";
            return false;
        }
        if (parsed.w < 0 || parsed.h < 0) {
            error = "sprite dimensions must not be negative";
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
        if (parsed.x < 0 || parsed.y < 0) {
            error = "sprite position must not be negative";
            return false;
        }
        if (parsed.w < 0 || parsed.h < 0) {
            error = "sprite dimensions must not be negative";
            return false;
        }
        if (tokens.size() == LEGACY_SPRITE_TOKENS_MAX) {
            if (!parse_int(tokens[4], parsed.src_x) || !parse_int(tokens[5], parsed.src_y)) {
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

bool parse_extrude_line(const std::string& line, int& extrude) {
    std::istringstream iss(line);
    std::string tag;
    std::string value_token;
    std::string extra;

    if (!(iss >> tag >> value_token)) {
        return false;
    }
    if (tag != "extrude") {
        return false;
    }
    if (!parse_int(value_token, extrude) || extrude < 0) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return true;
}

bool parse_multipack_line(const std::string& line, bool& multipack) {
    std::istringstream iss(line);
    std::string tag;
    std::string value_token;
    std::string extra;

    if (!(iss >> tag >> value_token)) {
        return false;
    }
    if (tag != "multipack") {
        return false;
    }
    if (value_token == "true" || value_token == "1") {
        multipack = true;
    } else if (value_token == "false" || value_token == "0") {
        multipack = false;
    } else {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return true;
}

bool parse_alias_line(const std::string& line, std::string& alias_path, std::string& canonical_path, std::string& error) {
    constexpr std::string_view prefix = "alias";
    if (!line.starts_with(prefix)) {
        error = "line does not start with alias";
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    if (pos >= line.size() || line[pos] != '"') {
        error = "alias path must be quoted";
        return false;
    }

    if (!parse_quoted(line, pos, alias_path, error)) {
        return false;
    }

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    if (pos >= line.size() || line[pos] != '"') {
        error = "canonical path must be quoted";
        return false;
    }

    if (!parse_quoted(line, pos, canonical_path, error)) {
        return false;
    }

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    if (pos < line.size()) {
        error = "extra content after canonical path";
        return false;
    }

    return true;
}

bool parse_layout(std::istream& in, Layout& out, std::string& error) {
    Layout parsed;
    std::string line;
    std::unordered_set<std::string> seen_sprite_paths;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (line.starts_with("atlas")) {
            int w = 0, h = 0;
            if (!parse_atlas_line(line, w, h)) {
                error = "Invalid atlas line: " + line;
                return false;
            }
            if (w <= 0 || h <= 0) {
                error = "Atlas dimensions must be positive: " + line;
                return false;
            }
            parsed.atlases.push_back({w, h});
        } else if (line.starts_with("root")) {
            if (parsed.has_root) {
                error = "Duplicate root line";
                return false;
            }
            size_t pos = 4;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
                ++pos;
            }
            std::string root_path;
            std::string root_error;
            if (!parse_quoted(line, pos, root_path, root_error)) {
                error = "Invalid root line: " + root_error;
                return false;
            }
            parsed.root = root_path;
            parsed.has_root = true;
        } else if (line.starts_with("scale")) {
            if (parsed.has_scale) {
                error = "Duplicate scale line";
                return false;
            }
            if (!parse_scale_line(line, parsed.scale)) {
                error = "Invalid scale line: " + line;
                return false;
            }
            parsed.has_scale = true;
        } else if (line.starts_with("extrude")) {
            if (parsed.has_extrude) {
                error = "Duplicate extrude line";
                return false;
            }
            if (!parse_extrude_line(line, parsed.extrude)) {
                error = "Invalid extrude line: " + line;
                return false;
            }
            parsed.has_extrude = true;
        } else if (line.starts_with("multipack")) {
            if (parsed.has_multipack) {
                error = "Duplicate multipack line";
                return false;
            }
            if (!parse_multipack_line(line, parsed.multipack)) {
                error = "Invalid multipack line: " + line;
                return false;
            }
            parsed.has_multipack = true;
        } else if (line.starts_with("sprite")) {
            Sprite s;
            std::string sprite_error;
            if (!parse_sprite_line(line, s, sprite_error)) {
                error = "Invalid sprite line: " + sprite_error;
                return false;
            }
            if (parsed.atlases.empty()) {
                error = "Sprite defined before any atlas";
                return false;
            }
            s.atlas_index = static_cast<int>(parsed.atlases.size()) - 1;
            const auto& atlas = parsed.atlases[static_cast<size_t>(s.atlas_index)];
            if (s.x + s.w > atlas.width || s.y + s.h > atlas.height) {
                error = "Sprite " + s.path + " extends beyond atlas bounds ("
                    + std::to_string(s.x) + "+" + std::to_string(s.w) + "="
                    + std::to_string(s.x + s.w) + " > " + std::to_string(atlas.width)
                    + " or " + std::to_string(s.y) + "+" + std::to_string(s.h) + "="
                    + std::to_string(s.y + s.h) + " > " + std::to_string(atlas.height) + ")";
                return false;
            }
            if (!parsed.multipack && !seen_sprite_paths.insert(s.path).second) {
                std::cerr << "Warning: duplicate sprite path: " << s.path << "\n";
            }
            parsed.sprites.push_back(s);
        } else if (line.starts_with("alias")) {
            std::string alias_path, canonical_path;
            if (!parse_alias_line(line, alias_path, canonical_path, error)) {
                error = "Invalid alias line: " + error;
                return false;
            }
            parsed.aliases.push_back({alias_path, canonical_path});
        } else if (is_combined_format_passthrough(line)) {
            // These lines are valid in the combined raw layout format but carry no
            // meaning for the basic layout parser. See is_combined_format_passthrough.
            continue;
        } else {
            // If the line is just whitespace, skip it
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            if (trimmed.empty()) {
                continue;
            }
            error = "Unknown line: " + line;
            return false;
        }
    }

    if (parsed.atlases.empty()) {
        error = "No atlas defined in layout";
        return false;
    }

    if (!parsed.has_scale) {
        parsed.scale = 1.0;
    }

    out = std::move(parsed);
    return true;
}

} // namespace sprat::core
