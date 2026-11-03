#include "layout_parser.h"
#include "cli_parse.h"
#include "i18n.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// Splits "a,b,c" into its comma-separated parts (empty parts kept, so "1,,2" fails later).
static std::vector<std::string> split_commas(const std::string& value) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t comma = value.find(',', start);
        parts.push_back(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            return parts;
        }
        start = comma + 1;
    }
}

// Reads exactly out.size() comma-separated integers.
static bool parse_int_list(const std::string& value, std::vector<int>& out) {
    const std::vector<std::string> parts = split_commas(value);
    if (parts.size() != out.size()) {
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!sprat::core::parse_int(parts[i], out[i])) {
            return false;
        }
    }
    return true;
}

static bool is_slice_mode(const std::string& mode) {
    return mode == "stretch" || mode == "repeat" || mode == "mirror" || mode == "fixed";
}

// "L,T,R,B" or "L,T,R,B,H_MODE,V_MODE": non-negative insets and optional fill modes.
static bool parse_slice(const std::string& val, int& left, int& top, int& right, int& bottom,
                        std::string& h_mode, std::string& v_mode) {
    const std::vector<std::string> parts = split_commas(val);
    if (parts.size() != 4 && parts.size() != 6) {
        return false;
    }
    if (!sprat::core::parse_non_negative_int(parts[0], left)
        || !sprat::core::parse_non_negative_int(parts[1], top)
        || !sprat::core::parse_non_negative_int(parts[2], right)
        || !sprat::core::parse_non_negative_int(parts[3], bottom)) {
        return false;
    }
    h_mode = parts.size() == 6 ? parts[4] : "stretch";
    v_mode = parts.size() == 6 ? parts[5] : "stretch";
    return is_slice_mode(h_mode) && is_slice_mode(v_mode);
}

// Reads "trim:L,T,R,B" (the value after "trim:"): non-negative margins.
static bool parse_trim_value(const std::string& value, int& left, int& top, int& right, int& bottom,
                             std::string& error) {
    std::vector<int> v(4);
    if (!parse_int_list(value, v)) {
        error = std::string(sprat::core::tr("invalid trim (expected trim:LEFT,TOP,RIGHT,BOTTOM): ")) + value;
        return false;
    }
    if (v[0] < 0 || v[1] < 0 || v[2] < 0 || v[3] < 0) {
        error = sprat::core::tr("trim offsets must not be negative");
        return false;
    }
    left = v[0];
    top = v[1];
    right = v[2];
    bottom = v[3];
    return true;
}

static void skip_spaces(const std::string& line, size_t& pos) {
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }
}

namespace sprat::core {

std::string metadata_line_keyword(const std::string& line) {
    std::istringstream iss(line);
    std::string first;
    iss >> first;
    if (first == "-") {
        std::string second;
        iss >> second;
        return "- " + second;
    }
    return first;
}

bool is_markers_keyword(std::string_view keyword) {
    return keyword == "root" || keyword == "path" || keyword == "- marker";
}

bool is_animations_keyword(std::string_view keyword) {
    return keyword == "root" || keyword == "fps" || keyword == "loop" || keyword == "animation"
        || keyword == "- frame";
}

bool is_puppets_keyword(std::string_view keyword) {
    return keyword == "root" || keyword == "puppet" || keyword == "part" || keyword == "attach"
        || keyword == "- sprite" || keyword == "- animation";
}

bool is_metadata_keyword(std::string_view keyword) {
    return is_markers_keyword(keyword) || is_animations_keyword(keyword) || is_puppets_keyword(keyword);
}


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

bool parse_rect_value(const std::string& value, int& x, int& y, int& w, int& h, std::string& error) {
    std::vector<int> v(4);
    if (!parse_int_list(value, v)) {
        error = std::string(tr("invalid rect (expected rect:X,Y,WIDTH,HEIGHT): ")) + value;
        return false;
    }
    if (v[0] < 0 || v[1] < 0) {
        error = tr("sprite position must not be negative");
        return false;
    }
    if (v[2] <= 0 || v[3] <= 0) {
        error = tr("sprite dimensions must be positive");
        return false;
    }
    x = v[0];
    y = v[1];
    w = v[2];
    h = v[3];
    return true;
}

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error) {
    constexpr std::string_view prefix = "sprite";
    if (!line.starts_with(prefix)) {
        error = tr("line does not start with sprite");
        return false;
    }

    size_t pos = prefix.size();
    skip_spaces(line, pos);
    if (pos >= line.size() || line[pos] != '"') {
        error = tr("sprite path must be quoted");
        return false;
    }
    Sprite parsed;
    if (!parse_quoted(line, pos, parsed.path, error)) {
        return false;
    }

    // After the path: rect:X,Y,W,H, then any of trim:L,T,R,B, rotated, dither, colors:N
    // and slice:L,T,R,B[,H,V], each at most once and in any order.
    bool has_rect = false;
    std::unordered_set<std::string> seen;
    std::istringstream tail(line.substr(pos));
    std::string token;
    while (tail >> token) {
        const size_t colon = token.find(':');
        const std::string key = token.substr(0, colon);
        const std::string value = colon == std::string::npos ? std::string() : token.substr(colon + 1);
        const bool takes_value = key == "rect" || key == "trim" || key == "colors" || key == "slice";
        const bool is_flag = key == "rotated" || key == "dither";
        if ((!takes_value && !is_flag) || (takes_value == (colon == std::string::npos))) {
            error = std::string(tr("unknown sprite option (expected rect:, trim:, rotated, dither, colors: or slice:): ")) + token;
            return false;
        }
        if (!seen.insert(key).second) {
            error = std::string(tr("sprite option given twice: ")) + key;
            return false;
        }
        if (key == "rect") {
            if (!parse_rect_value(value, parsed.x, parsed.y, parsed.w, parsed.h, error)) {
                return false;
            }
            has_rect = true;
        } else if (key == "trim") {
            if (!parse_trim_value(value, parsed.src_x, parsed.src_y, parsed.trim_right,
                                  parsed.trim_bottom, error)) {
                return false;
            }
            parsed.has_trim = true;
        } else if (key == "rotated") {
            parsed.rotated = true;
        } else if (key == "dither") {
            parsed.dither = true;
        } else if (key == "colors") {
            if (!parse_int(value, parsed.colors) || (parsed.colors != 0 && (parsed.colors < 2 || parsed.colors > 256))) {
                error = std::string(tr("invalid colors value (must be 0 or 2-256): ")) + value;
                return false;
            }
        } else {
            if (!parse_slice(value, parsed.slice_left, parsed.slice_top,
                             parsed.slice_right, parsed.slice_bottom,
                             parsed.slice_h, parsed.slice_v)) {
                error = std::string(tr("invalid slice value (expected L,T,R,B[,H_MODE,V_MODE] with non-negative integers and optional stretch/repeat/mirror/fixed modes): ")) + value;
                return false;
            }
            parsed.has_slice = true;
        }
    }
    if (!has_rect) {
        error = tr("sprite line has no rect:X,Y,WIDTH,HEIGHT");
        return false;
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
        return false;
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
    if (value_token == "true") {
        multipack = true;
    } else if (value_token == "false") {
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

    // alias "ORIGINAL" -> "DUPLICATE" [trim:L,T,R,B]
    size_t pos = prefix.size();
    skip_spaces(line, pos);
    Alias parsed;
    if (pos >= line.size() || line[pos] != '"') {
        error = tr("original path must be quoted");
        return false;
    }
    if (!parse_quoted(line, pos, parsed.canonical_path, error)) {
        return false;
    }
    skip_spaces(line, pos);
    if (line.compare(pos, 2, "->") != 0) {
        error = tr("expected -> between the original and the duplicate path");
        return false;
    }
    pos += 2;
    skip_spaces(line, pos);
    if (pos >= line.size() || line[pos] != '"') {
        error = tr("duplicate path must be quoted");
        return false;
    }
    if (!parse_quoted(line, pos, parsed.path, error)) {
        return false;
    }

    std::istringstream tail(line.substr(pos));
    std::string token;
    while (tail >> token) {
        if (!token.starts_with("trim:") || parsed.has_trim) {
            error = std::string(tr("unexpected alias option (expected one trim:LEFT,TOP,RIGHT,BOTTOM): ")) + token;
            return false;
        }
        if (!parse_trim_value(token.substr(5), parsed.trim_left, parsed.trim_top,
                              parsed.trim_right, parsed.trim_bottom, error)) {
            return false;
        }
        parsed.has_trim = true;
    }

    out = std::move(parsed);
    return true;
}

bool parse_layout(std::istream& in, Layout& out, std::string& error) {
    Layout parsed;
    std::string line;
    std::unordered_set<std::string> seen_sprite_paths;

    while (std::getline(in, line)) {
        const size_t first_char = line.find_first_not_of(" \t\r");
        if (first_char == std::string::npos || line[first_char] == '#') {
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
            if (line.find_first_not_of(" \t\r", pos) != std::string::npos) {
                error = std::string(tr("Invalid root line: ")) + tr("extra content after the path");
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
                parsed.warnings.push_back(std::string(tr("duplicate sprite path: ")) + s.path);
            }

            parsed.sprites.push_back(s);
        } else if (line.starts_with("alias")) {
            Alias alias;
            if (!parse_alias_line(line, alias, error)) {
                error = std::string(tr("Invalid alias line: ")) + error;
                return false;
            }
            parsed.aliases.push_back(std::move(alias));
        } else if (is_metadata_keyword(metadata_line_keyword(line))) {
            // Markers, animations and puppets lines, which spratconvert reads from the
            // layout stream; they mean nothing to the layout itself. Matched by whole
            // keyword, so a mistyped line is still reported.
            continue;
        } else {
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
