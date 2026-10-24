#include "layout_parser.h"
#include "cli_parse.h"
#include "i18n.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
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

    if (h_mode != "stretch" && h_mode != "repeat" && h_mode != "mirror" && h_mode != "fixed") return false;
    if (v_mode != "stretch" && v_mode != "repeat" && v_mode != "mirror" && v_mode != "fixed") return false;

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

int scale_to_atlas(int value, double scale) {
    if (scale == 1.0) {
        return value;
    }
    const long double scaled = std::round(static_cast<long double>(value) * scale);
    if (scaled >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (scaled <= static_cast<long double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(scaled);
}

MetadataTrim metadata_trim(const Sprite& s, double scale) {
    MetadataTrim t{s.src_x, s.src_y, s.trim_right, s.trim_bottom};
    if (is_nine_patch_path(s.path) &&
        t.left >= 1 && t.top >= 1 && t.right >= 1 && t.bottom >= 1) {
        --t.left;
        --t.top;
        --t.right;
        --t.bottom;
    }
    return {scale_to_atlas(t.left, scale), scale_to_atlas(t.top, scale),
            scale_to_atlas(t.right, scale), scale_to_atlas(t.bottom, scale)};
}

// Largest original size that spratlayout packs into `packed` atlas pixels. It rounds the
// scaled size to the nearest pixel (at least 1), so the original may be up to half an
// atlas pixel larger than packed / scale.
static long long max_original_size(int packed, double scale) {
    if (scale == 1.0) {
        return packed;
    }
    const long double original = std::floor((static_cast<long double>(packed) + 0.5L) / scale);
    if (original >= static_cast<long double>(std::numeric_limits<long long>::max())) {
        return std::numeric_limits<long long>::max();
    }
    return static_cast<long long>(original);
}

// Shrinks `second` so first + second <= limit, after capping `first` at limit.
static void fit_inset_pair(int& first, int& second, int limit) {
    limit = std::max(limit, 0);
    first = std::clamp(first, 0, limit);
    second = std::clamp(second, 0, limit - first);
}

MetadataSlice metadata_slice(const Sprite& s, double scale, int source_w, int source_h) {
    MetadataSlice out{scale_to_atlas(s.slice_left, scale), scale_to_atlas(s.slice_top, scale),
                      scale_to_atlas(s.slice_right, scale), scale_to_atlas(s.slice_bottom, scale)};
    fit_inset_pair(out.left, out.right, source_w);
    fit_inset_pair(out.top, out.bottom, source_h);
    return out;
}

bool check_sprite_limits(const Sprite& s, double scale, std::string& error) {
    const MetadataTrim trim = metadata_trim(s, scale);
    const long long source_w = static_cast<long long>(s.rotated ? s.h : s.w) + trim.left + trim.right;
    const long long source_h = static_cast<long long>(s.rotated ? s.w : s.h) + trim.top + trim.bottom;
    if (!within_coordinate_limit(source_w) || !within_coordinate_limit(source_h)) {
        error = std::string(tr("Sprite ")) + s.path + tr(" is larger than ")
            + std::to_string(k_max_coordinate) + tr(" pixels once its trim is restored");
        return false;
    }
    return true;
}

bool check_slice_fits(const Sprite& s, double scale, std::string& error) {
    if (!s.has_slice) {
        return true;
    }
    const MetadataTrim trim = metadata_trim(s, 1.0); // original-image pixels
    const long long width = max_original_size(s.rotated ? s.h : s.w, scale);
    const long long height = max_original_size(s.rotated ? s.w : s.h, scale);
    const long long source_w = width > std::numeric_limits<long long>::max() / 2
        ? width : width + trim.left + trim.right;
    const long long source_h = height > std::numeric_limits<long long>::max() / 2
        ? height : height + trim.top + trim.bottom;
    if (static_cast<long long>(s.slice_left) + s.slice_right > source_w) {
        error = std::string(tr("Slice insets of sprite ")) + s.path + tr(" are wider than the image (")
            + std::to_string(s.slice_left) + "+" + std::to_string(s.slice_right) + " > "
            + std::to_string(source_w) + ")";
        return false;
    }
    if (static_cast<long long>(s.slice_top) + s.slice_bottom > source_h) {
        error = std::string(tr("Slice insets of sprite ")) + s.path + tr(" are taller than the image (")
            + std::to_string(s.slice_top) + "+" + std::to_string(s.slice_bottom) + " > "
            + std::to_string(source_h) + ")";
        return false;
    }
    return true;
}

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error) {
    constexpr std::string_view prefix = "sprite";
    if (!line.starts_with(prefix)) {
        error = tr("line does not start with sprite");
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    std::string path;
    if (pos >= line.size() || line[pos] != '"') {
        error = tr("sprite path must be quoted");
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
                error = std::string(tr("invalid colors value (must be 0 or 2-256): ")) + val;
                return false;
            }
        } else if (token.starts_with("slice=")) {
            std::string val = token.substr(6);
            if (!parse_slice(val, parsed.slice_left, parsed.slice_top,
                             parsed.slice_right, parsed.slice_bottom,
                             parsed.slice_h, parsed.slice_v)) {
                error = std::string(tr("invalid slice value (expected L,T,R,B[,H_MODE,V_MODE] with non-negative integers and optional stretch/repeat/mirror/fixed modes): ")) + val;
                return false;
            }
            parsed.has_slice = true;
        } else {
            tokens.push_back(token);
        }
    }

    if (tokens.empty()) {
        error = tr("sprite line is missing numeric fields");
        return false;
    }

    if (tokens[0].find(',') != std::string::npos) {
        constexpr size_t MODERN_SPRITE_TOKENS_MIN = 2;
        constexpr size_t MODERN_SPRITE_TOKENS_MAX = 4;
        if (tokens.size() != MODERN_SPRITE_TOKENS_MIN && tokens.size() != MODERN_SPRITE_TOKENS_MAX) {
            error = tr("sprite line must contain position/size and optional trim offsets");
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y) || !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = tr("invalid position or size pair");
            return false;
        }
        if (parsed.x < 0 || parsed.y < 0) {
            error = tr("sprite position must not be negative");
            return false;
        }
        if (parsed.w < 0 || parsed.h < 0) {
            error = tr("sprite dimensions must not be negative");
            return false;
        }

        if (tokens.size() == MODERN_SPRITE_TOKENS_MAX) {
            if (!parse_pair(tokens[2], parsed.src_x, parsed.src_y)
                || !parse_pair(tokens[3], parsed.trim_right, parsed.trim_bottom)) {
                error = tr("invalid trim offset pair");
                return false;
            }
            if (parsed.src_x < 0 || parsed.src_y < 0 || parsed.trim_right < 0 || parsed.trim_bottom < 0) {
                error = tr("trim offsets must not be negative");
                return false;
            }
            parsed.has_trim = true;
        }
    } else {
        constexpr size_t LEGACY_SPRITE_TOKENS_MIN = 4;
        constexpr size_t LEGACY_SPRITE_TOKENS_MAX = 6;
        if (tokens.size() != LEGACY_SPRITE_TOKENS_MIN && tokens.size() != LEGACY_SPRITE_TOKENS_MAX) {
            error = tr("legacy sprite line has invalid field count");
            return false;
        }
        if (!parse_int(tokens[0], parsed.x)
            || !parse_int(tokens[1], parsed.y)
            || !parse_int(tokens[2], parsed.w)
            || !parse_int(tokens[3], parsed.h)) {
            error = tr("legacy sprite line has invalid numeric fields");
            return false;
        }
        if (parsed.x < 0 || parsed.y < 0) {
            error = tr("sprite position must not be negative");
            return false;
        }
        if (parsed.w < 0 || parsed.h < 0) {
            error = tr("sprite dimensions must not be negative");
            return false;
        }
        if (tokens.size() == LEGACY_SPRITE_TOKENS_MAX) {
            if (!parse_int(tokens[4], parsed.src_x) || !parse_int(tokens[5], parsed.src_y)) {
                error = tr("legacy sprite line has invalid crop offsets");
                return false;
            }
            if (parsed.src_x < 0 || parsed.src_y < 0) {
                error = tr("trim offsets must not be negative");
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

bool parse_rotation_line(const std::string& line, bool& rotate_ccw) {
    std::istringstream iss(line);
    std::string tag;
    std::string value_token;
    std::string extra;

    if (!(iss >> tag >> value_token)) {
        return false;
    }
    if (tag != "rotation") {
        return false;
    }
    if (value_token == "cw") {
        rotate_ccw = false;
    } else if (value_token == "ccw") {
        rotate_ccw = true;
    } else {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return true;
}

bool parse_alias_line(const std::string& line, Alias& out, std::string& error) {
    constexpr std::string_view prefix = "alias";
    if (!line.starts_with(prefix)) {
        error = tr("line does not start with alias");
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    if (pos >= line.size() || line[pos] != '"') {
        error = tr("alias path must be quoted");
        return false;
    }

    Alias parsed;
    if (!parse_quoted(line, pos, parsed.path, error)) {
        return false;
    }

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    if (pos >= line.size() || line[pos] != '"') {
        error = tr("canonical path must be quoted");
        return false;
    }

    if (!parse_quoted(line, pos, parsed.canonical_path, error)) {
        return false;
    }

    // Optional trim offsets, same syntax as on sprite lines: L,T R,B
    std::istringstream tail(line.substr(pos));
    std::vector<std::string> tokens;
    std::string token;
    while (tail >> token) {
        tokens.push_back(token);
    }
    if (tokens.size() == 2) {
        if (!parse_pair(tokens[0], parsed.trim_left, parsed.trim_top)
            || !parse_pair(tokens[1], parsed.trim_right, parsed.trim_bottom)) {
            error = tr("invalid alias trim offset pair");
            return false;
        }
        if (parsed.trim_left < 0 || parsed.trim_top < 0
            || parsed.trim_right < 0 || parsed.trim_bottom < 0) {
            error = tr("alias trim offsets must not be negative");
            return false;
        }
        parsed.has_trim = true;
    } else if (!tokens.empty()) {
        error = tr("extra content after canonical path");
        return false;
    }

    out = std::move(parsed);
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
                error = std::string(tr("Invalid atlas line: ")) + line;
                return false;
            }
            if (w <= 0 || h <= 0) {
                error = std::string(tr("Atlas dimensions must be positive: ")) + line;
                return false;
            }
            if (w > k_max_coordinate || h > k_max_coordinate) {
                error = std::string(tr("Atlas dimensions exceed ")) + std::to_string(k_max_coordinate)
                    + ": " + line;
                return false;
            }
            parsed.atlases.push_back({w, h});
        } else if (line.starts_with("root")) {
            if (parsed.has_root) {
                error = tr("Duplicate root line");
                return false;
            }
            size_t pos = 4;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
                ++pos;
            }
            std::string root_path;
            std::string root_error;
            if (!parse_quoted(line, pos, root_path, root_error)) {
                error = std::string(tr("Invalid root line: ")) + root_error;
                return false;
            }
            parsed.root = root_path;
            parsed.has_root = true;
        } else if (line.starts_with("scale")) {
            if (parsed.has_scale) {
                error = tr("Duplicate scale line");
                return false;
            }
            if (!parse_scale_line(line, parsed.scale)) {
                error = std::string(tr("Invalid scale line: ")) + line;
                return false;
            }
            parsed.has_scale = true;
        } else if (line.starts_with("extrude")) {
            if (parsed.has_extrude) {
                error = tr("Duplicate extrude line");
                return false;
            }
            if (!parse_extrude_line(line, parsed.extrude)) {
                error = std::string(tr("Invalid extrude line: ")) + line;
                return false;
            }
            if (parsed.extrude > k_max_coordinate) {
                error = std::string(tr("Extrude exceeds ")) + std::to_string(k_max_coordinate) + ": " + line;
                return false;
            }
            parsed.has_extrude = true;
        } else if (line.starts_with("multipack")) {
            if (parsed.has_multipack) {
                error = tr("Duplicate multipack line");
                return false;
            }
            if (!parse_multipack_line(line, parsed.multipack)) {
                error = std::string(tr("Invalid multipack line: ")) + line;
                return false;
            }
            parsed.has_multipack = true;
        } else if (line.starts_with("rotation")) {
            if (parsed.has_rotation) {
                error = tr("Duplicate rotation line");
                return false;
            }
            if (!parse_rotation_line(line, parsed.rotate_ccw)) {
                error = std::string(tr("Invalid rotation line (expected cw or ccw): ")) + line;
                return false;
            }
            parsed.has_rotation = true;
        } else if (line.starts_with("sprite")) {
            Sprite s;
            std::string sprite_error;
            if (!parse_sprite_line(line, s, sprite_error)) {
                error = std::string(tr("Invalid sprite line: ")) + sprite_error;
                return false;
            }
            if (parsed.atlases.empty()) {
                error = tr("Sprite defined before any atlas");
                return false;
            }
            s.atlas_index = static_cast<int>(parsed.atlases.size()) - 1;
            const auto& atlas = parsed.atlases[static_cast<size_t>(s.atlas_index)];
            // Compare without adding so huge coordinates can't overflow past the check.
            if (s.x > atlas.width || s.w > atlas.width - s.x
                || s.y > atlas.height || s.h > atlas.height - s.y) {
                error = std::string(tr("Sprite ")) + s.path + tr(" extends beyond atlas bounds (")
                    + std::to_string(s.x) + "+" + std::to_string(s.w) + "="
                    + std::to_string(static_cast<long long>(s.x) + s.w) + " > " + std::to_string(atlas.width)
                    + " or " + std::to_string(s.y) + "+" + std::to_string(s.h) + "="
                    + std::to_string(static_cast<long long>(s.y) + s.h) + " > " + std::to_string(atlas.height) + ")";
                return false;
            }
            if (!parsed.multipack && !seen_sprite_paths.insert(s.path).second) {
                std::cerr << tr("Warning: duplicate sprite path: ") << s.path << "\n";
            }
            parsed.sprites.push_back(s);
        } else if (line.starts_with("alias")) {
            Alias alias;
            if (!parse_alias_line(line, alias, error)) {
                error = std::string(tr("Invalid alias line: ")) + error;
                return false;
            }
            parsed.aliases.push_back(std::move(alias));
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
            error = std::string(tr("Unknown line: ")) + line;
            return false;
        }
    }

    if (parsed.atlases.empty()) {
        error = tr("No atlas defined in layout");
        return false;
    }

    if (!parsed.has_scale) {
        parsed.scale = 1.0;
    }

    // Checked once the scale is known, since it may follow the sprite lines. Aliases are
    // not checked: they inherit the canonical sprite's insets with their own trim, and
    // spratconvert fits the insets to every sprite anyway (see metadata_slice).
    for (const Sprite& s : parsed.sprites) {
        if (!check_sprite_limits(s, parsed.scale, error) || !check_slice_fits(s, parsed.scale, error)) {
            return false;
        }
    }

    out = std::move(parsed);
    return true;
}

bool expand_layout_aliases(Layout& layout, std::string& error) {
    std::unordered_map<std::string, size_t> index_by_path;
    index_by_path.reserve(layout.sprites.size());
    for (size_t i = 0; i < layout.sprites.size(); ++i) {
        index_by_path.emplace(layout.sprites[i].path, i);
    }
    layout.sprites.reserve(layout.sprites.size() + layout.aliases.size());
    for (const Alias& alias : layout.aliases) {
        const auto it = index_by_path.find(alias.canonical_path);
        if (it == index_by_path.end()) {
            error = std::string(tr("Alias ")) + "\"" + alias.path + "\"" + tr(" refers to unknown sprite ")
                + "\"" + alias.canonical_path + "\"";
            return false;
        }
        Sprite expanded = layout.sprites[it->second];
        expanded.path = alias.path;
        expanded.alias_of = alias.canonical_path;
        if (alias.has_trim) {
            expanded.src_x = alias.trim_left;
            expanded.src_y = alias.trim_top;
            expanded.trim_right = alias.trim_right;
            expanded.trim_bottom = alias.trim_bottom;
            expanded.has_trim = true;
        }
        // An alias keeps its own trim, which may make it larger than its canonical sprite.
        if (!check_sprite_limits(expanded, layout.scale, error)) {
            return false;
        }
        layout.sprites.push_back(std::move(expanded));
    }
    layout.aliases.clear();
    return true;
}

} // namespace sprat::core
