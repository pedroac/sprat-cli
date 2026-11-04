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
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
namespace fs = std::filesystem;
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <system_error>
#include "core/layout_parser.h"
#include "core/cli_parse.h"
#include "core/file_io.h"
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
// Most frames per second an animation may play at. Sprite animations run far slower, so a
// larger value is a mistake. It matches sprat-lib's SPRAT_FPS_LIMIT, so every sheet
// spratconvert writes also loads there.
constexpr int k_max_animation_fps = 100;
constexpr int k_default_precision = 8;
constexpr size_t k_string_growth_padding = 8;
constexpr unsigned char k_json_control_char_limit = 0x20;
constexpr std::uint8_t HEX_NIBBLE_MASK = 0x0f;
constexpr int BITS_PER_NIBBLE = 4;
constexpr const char* HEX_DIGITS = "0123456789abcdef";

// How an animation plays: "loop" repeats it, "once" plays it once and holds the last
// frame, "pingpong" plays it forward then backward and repeats, and "once-pingpong" plays
// forward then backward once and holds the first frame.
constexpr const char* DEFAULT_ANIMATION_LOOP = "loop";
constexpr const char* k_animation_loop_modes = "loop, once, pingpong, once-pingpong";

bool is_animation_loop_mode(const std::string& token) {
    return token == "loop" || token == "once" || token == "pingpong" || token == "once-pingpong";
}

struct AnimationItem {
    size_t index = 0;
    std::string name;
    std::vector<int> sprite_indexes;
    int fps = DEFAULT_ANIMATION_FPS;
    std::string loop = DEFAULT_ANIMATION_LOOP;
    std::string alias_source;
    std::string flip;
    // The regular animation an alias replays, through any chain of aliases (its own index
    // for a regular one); -1 until resolve_alias_sources, or for an unknown source or a cycle.
    int source_index = -1;
};

using Sprite = sprat::core::Sprite;
using Layout = sprat::core::Layout;
using sprat::core::format_index_pattern;
using sprat::core::parse_int;
using sprat::core::is_animations_keyword;
using sprat::core::is_markers_keyword;
using sprat::core::is_metadata_keyword;
using sprat::core::is_puppets_keyword;
using sprat::core::parse_layout;
using sprat::core::parse_pair;
using sprat::core::parse_quoted;
using sprat::core::tr;
using sprat::core::trim_copy;
using sprat::core::validate_output_pattern;

bool read_text_file(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = tr("Failed to open file: ") + path.string();
        return false;
    }
    const auto size = in.tellg();
    if (size < 0) {
        error = tr("Failed to read file: ") + path.string();
        return false;
    }
    in.seekg(0);
    out.resize(static_cast<size_t>(size));
    in.read(out.data(), size);
    if (!in.good() && !in.eof()) {
        error = tr("Failed to read file: ") + path.string();
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

// Sprite lookups match path suffixes split on either separator, so metadata
// files written on one platform (or joined with a root on Windows, where
// fs::path uses '\\') still resolve against layout paths that use '/'.
constexpr const char* kPathSeparators = "/\\";

// The file name of a layout sprite path, split on '/' and '\\' by hand: fs::path on Windows
// would read "a: b.png" as drive "a:" plus " b.png", and the name must not depend on the OS.
std::string layout_path_filename(const std::string& path) {
    const size_t sep = path.find_last_of(kPathSeparators);
    return sep == std::string::npos ? path : path.substr(sep + 1);
}

std::string sprite_name_from_path(const std::string& path) {
    const std::string filename = layout_path_filename(path);
    // Like fs::path::stem(): drop the last extension, but keep a leading dot (".hidden").
    const size_t dot = filename.rfind('.');
    std::string name = (dot == std::string::npos || dot == 0 || filename == "..")
                           ? filename
                           : filename.substr(0, dot);
    // Strip the ".9" from Android nine-patch stems ("button.9" → "button").
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, ".9") == 0 &&
        sprat::core::is_nine_patch_path(path)) {
        name.resize(name.size() - 2);
    }
    if (!name.empty()) {
        return name;
    }
    if (!filename.empty()) {
        return filename;
    }
    return path;
}

// Joins a metadata-file `root` with a relative path using '/' on every
// platform, matching how layout sprite paths are written.
std::string join_metadata_root(const std::string& root, const std::string& path) {
    if (root.empty() || !fs::path(path).is_relative()) {
        return path;
    }
    return (fs::path(root) / path).generic_string();
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
        by_path[layout_path_filename(s.path)] = idx;
        size_t sep = s.path.find_first_of(kPathSeparators);
        while (sep != std::string::npos) {
            ++sep;
            std::string suffix = s.path.substr(sep);
            if (!suffix.empty()) {
                by_path.emplace(suffix, idx);
            }
            sep = s.path.find_first_of(kPathSeparators, sep);
        }
        std::string name = sprite_name_from_path(s.path);
        sprite_names.push_back(name);
        by_name[name] = idx;
        ++idx;
    }
}

// Finds the sprite a metadata file names: a path, resolved against the file's root, or a
// sprite name, which a root doesn't change ("run_01" under root "./frames" is still run_01).
int resolve_sprite_index(const std::string& root, const std::string& key,
                         const std::unordered_map<std::string, int>& by_path,
                         const std::unordered_map<std::string, int>& by_name) {
    const std::string path = join_metadata_root(root, key);
    auto by_path_it = by_path.find(path);
    if (by_path_it != by_path.end()) {
        return by_path_it->second;
    }
    size_t sep = path.find_first_of(kPathSeparators);
    while (sep != std::string::npos) {
        ++sep;
        if (sep < path.size()) {
            auto it = by_path.find(path.substr(sep));
            if (it != by_path.end()) {
                return it->second;
            }
        }
        sep = path.find_first_of(kPathSeparators, sep);
    }
    auto by_name_it = by_name.find(key);
    if (by_name_it != by_name.end()) {
        return by_name_it->second;
    }
    return -1;
}

// Marker files are hand-written, so their values can be anything an int holds. Fails when
// a value, once scaled to atlas pixels, would exceed sprat::core::k_max_coordinate.
bool check_marker_limits(const MarkerItem& m, double scale, std::string& error) {
    const auto fits = [&](int v) {
        return sprat::core::within_coordinate_limit(sprat::core::scale_to_atlas(v, scale));
    };
    bool ok = fits(m.x) && fits(m.y) && fits(m.radius) && fits(m.w) && fits(m.h);
    for (const auto& [vx, vy] : m.vertices) {
        ok = ok && fits(vx) && fits(vy);
    }
    if (!ok) {
        error = std::string(tr("Marker ")) + "\"" + m.name + "\""
            + (m.sprite_path.empty() ? std::string() : std::string(tr(" of sprite ")) + m.sprite_path)
            + tr(" has a value beyond ") + std::to_string(sprat::core::k_max_coordinate)
            + tr(" atlas pixels");
        return false;
    }
    return true;
}

// ---- Hand-written markers and animations files ------------------------------------------
//
// These files are written by hand, so every line must mean something: a line that is not
// one of the file's own is reported, never skipped. Without --markers, --animations or
// --puppets, they are read from the layout stream, which also holds layout lines and the
// other files' lines; only then are those skipped.

bool is_layout_keyword(const std::string& keyword) {
    return keyword == "atlas" || keyword == "scale" || keyword == "extrude" || keyword == "multipack"
        || keyword == "rotation" || keyword == "sprite" || keyword == "alias";
}

// Reads a `root "DIR"` line into root. Reports and ignores one that isn't exactly that.
void read_root_line(const std::string& trimmed, std::string& root, const char* file,
                    std::vector<std::string>& warnings) {
    size_t pos = 4;
    while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
        ++pos;
    }
    std::string value;
    std::string error;
    if (pos >= trimmed.size() || trimmed[pos] != '"' || !parse_quoted(trimmed, pos, value, error)
        || !trim_copy(trimmed.substr(pos)).empty()) {
        warnings.push_back(sprat::core::tr_format(
            "{0} line \"{1}\" is not root \"DIRECTORY\"; it is ignored", {file, trimmed}));
        return;
    }
    root = value;
}

// Reads a name, quoted or bare, starting at `pos` (after any spaces). A bare name ends at
// any of `stops`. On success `pos` is just past it; `quoted` tells which it was. An empty
// name ("") fails: the raw format writes it as "none".
bool read_name_at(const std::string& line, size_t& pos, std::string& name,
                  const char* stops = " \t", bool* quoted = nullptr) {
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    const bool is_quoted = pos < line.size() && line[pos] == '"';
    if (quoted != nullptr) {
        *quoted = is_quoted;
    }
    if (is_quoted) {
        std::string quote_error;
        return parse_quoted(line, pos, name, quote_error) && !name.empty();
    }
    const size_t end = std::min(line.size(), line.find_first_of(stops, pos));
    name = line.substr(pos, end - pos);
    pos = end;
    return !name.empty();
}

// Reads the shape after a marker's name and type ("x,y", "x,y radius", "x,y w,h" or a
// list of "x,y" vertices). False when the values don't match the type, extra ones included.
bool read_marker_shape(std::istringstream& rest, MarkerItem& item) {
    std::string pt;
    std::string extra;
    if (item.type == "point") {
        return rest >> pt && parse_pair(pt, item.x, item.y) && !(rest >> extra);
    }
    if (item.type == "circle") {
        std::string radius;
        return rest >> pt && parse_pair(pt, item.x, item.y)
            && rest >> radius && parse_int(radius, item.radius) && !(rest >> extra);
    }
    if (item.type == "rectangle") {
        std::string sz;
        return rest >> pt && parse_pair(pt, item.x, item.y)
            && rest >> sz && parse_pair(sz, item.w, item.h) && !(rest >> extra);
    }
    if (item.type == "polygon") {
        while (rest >> pt) {
            int vx = 0;
            int vy = 0;
            if (!parse_pair(pt, vx, vy)) {
                return false;
            }
            item.vertices.emplace_back(vx, vy);
        }
        return !item.vertices.empty();
    }
    return false;
}

std::vector<MarkerItem> parse_markers_data(const std::string& markers_text,
                                           bool from_layout_stream,
                                           const Layout& layout,
                                           const std::unordered_map<std::string, int>& by_path,
                                           const std::unordered_map<std::string, int>& by_name,
                                           const std::vector<std::string>& sprite_names,
                                           std::vector<std::vector<MarkerItem>>& sprite_markers,
                                           std::vector<std::string>& warnings) {
    sprite_markers.assign(layout.sprites.size(), {});
    std::vector<MarkerItem> markers;
    std::istringstream iss(markers_text);
    std::string line;
    int current_sprite_index = -1;
    // Set after a "path" that matches no sprite: its markers are dropped rather than
    // made global, where a mistyped path would hand its pivot to every sprite.
    bool skipping_unknown_path = false;
    std::string raw_root;
    // Sprites (-1 for global) that already have a pivot: only the first one is used.
    std::unordered_set<int> has_pivot;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::string keyword = sprat::core::metadata_line_keyword(trimmed);
        if (!is_markers_keyword(keyword)) {
            if (!from_layout_stream || !(is_layout_keyword(keyword) || is_metadata_keyword(keyword))) {
                warnings.push_back(sprat::core::tr_format(
                    "Markers file line \"{0}\" is not a root, path or - marker line; it is ignored",
                    {trimmed}));
            }
            continue;
        }

        if (keyword == "root") {
            read_root_line(trimmed, raw_root, "Markers file", warnings);
        } else if (keyword == "path") {
            std::string path;
            size_t pos = 4;
            const bool has_path = read_name_at(trimmed, pos, path);
            current_sprite_index = -1;
            if (!has_path || !trim_copy(trimmed.substr(pos)).empty()) {
                warnings.push_back(sprat::core::tr_format(
                    "Markers file line \"{0}\" is not path \"SPRITE\"; its markers are ignored",
                    {trimmed}));
            } else {
                current_sprite_index =
                    resolve_sprite_index(raw_root, path, by_path, by_name);
                if (current_sprite_index < 0) {
                    warnings.push_back(sprat::core::tr_format(
                        "Markers file path \"{0}\" matches no sprite; its markers are ignored",
                        {path}));
                }
            }
            skipping_unknown_path = current_sprite_index < 0;
        } else {
            if (skipping_unknown_path) {
                continue;  // already reported with the path
            }
            size_t pos = trimmed.find("marker") + 6;
            std::string name;
            if (!read_name_at(trimmed, pos, name)) {
                warnings.push_back(sprat::core::tr_format(
                    "Marker line \"{0}\" has no valid name; it is ignored", {trimmed}));
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

            std::istringstream rest(trimmed.substr(pos));
            if (!(rest >> item.type)) {
                warnings.push_back(sprat::core::tr_format(
                    "Marker \"{0}\" has no type; it is ignored", {name}));
                continue;
            }
            if (item.type != "point" && item.type != "circle" && item.type != "rectangle"
                && item.type != "polygon") {
                warnings.push_back(sprat::core::tr_format(
                    "Marker \"{0}\" has an unknown type \"{1}\" (expected point, circle, rectangle or polygon); it is ignored",
                    {name, item.type}));
                continue;
            }
            if (!read_marker_shape(rest, item)) {
                warnings.push_back(sprat::core::tr_format(
                    "Marker \"{0}\" has invalid {1} values; it is ignored", {name, item.type}));
                continue;
            }
            if (item.name == "pivot" && item.type == "point"
                && !has_pivot.insert(current_sprite_index).second) {
                warnings.push_back(current_sprite_index < 0
                    ? std::string(tr("There is more than one global pivot; only the first is used"))
                    : sprat::core::tr_format("Sprite \"{0}\" has more than one pivot; only the first is used",
                                             {item.sprite_path}));
            }

            if (current_sprite_index >= 0) {
                sprite_markers[static_cast<size_t>(current_sprite_index)].push_back(item);
            }
            markers.push_back(std::move(item));
        }
    }
    return markers;
}

// Reads an fps number from a hand-written animations file the way `stream >> int` does:
// an optional sign, then the leading digits, so "12.5" reads 12 and "+24" reads 24; false
// when there are no digits. `partial` tells whether anything follows the digits, so the
// caller can warn that "12.5" or a typo such as "1O0" was cut short. A number too large
// for an int is still above k_max_animation_fps, so it saturates to INT_MAX instead of
// being dropped (and a too-negative one to INT_MIN, the default).
bool read_animation_fps(const std::string& token, int& out, bool& partial) {
    size_t digits_begin = 0;
    if (!token.empty() && (token[0] == '+' || token[0] == '-')) {
        digits_begin = 1;
    }
    size_t digits_end = digits_begin;
    while (digits_end < token.size()
           && std::isdigit(static_cast<unsigned char>(token[digits_end]))) {
        ++digits_end;
    }
    if (digits_end == digits_begin) {
        return false;
    }
    const bool negative = token[0] == '-';
    // from_chars takes a '-' but not a '+', so a '+' is skipped.
    const char* number_begin = token.data() + (negative ? 0 : digits_begin);
    int value = 0;
    const auto [ptr, ec] = std::from_chars(number_begin, token.data() + digits_end, value);
    if (ec == std::errc::result_out_of_range) {
        value = negative ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    } else if (ec != std::errc()) {
        return false;
    }
    out = value;
    partial = digits_end < token.size();
    return true;
}

// An alias's "flip" value: "h", "v" or both ("hv" or "vh", written "hv"). Empty when the
// value is none of them.
std::string normalize_flip(const std::string& value) {
    if (value == "h" || value == "v" || value == "hv") {
        return value;
    }
    if (value == "vh") {
        return "hv";
    }
    return {};
}

// Most frames one range may list. Sprite sheets hold far fewer, so a larger range is a typo.
constexpr long long k_max_frame_range = 100000;

// One item of a "- frame" line: a sprite (path, name or index), or a range of them.
struct FrameItem {
    std::string first;
    std::string last;  // set for a range "FIRST..LAST"
    bool quoted = false;
    bool is_range = false;
};

// A name split around its last run of digits: "hero/run_01.png" is "hero/run_", "01", ".png".
struct NumberedName {
    std::string prefix;
    std::string digits;
    std::string suffix;
};

bool split_numbered_name(const std::string& name, NumberedName& out) {
    const size_t last_digit = name.find_last_of("0123456789");
    if (last_digit == std::string::npos) {
        return false;
    }
    size_t first_digit = last_digit;
    while (first_digit > 0 && std::isdigit(static_cast<unsigned char>(name[first_digit - 1]))) {
        --first_digit;
    }
    out.prefix = name.substr(0, first_digit);
    out.digits = name.substr(first_digit, last_digit + 1 - first_digit);
    out.suffix = name.substr(last_digit + 1);
    return out.digits.size() <= 9;  // fits an int
}

// Whether "first..last" reads as a range of sprite names: the same name around a number.
bool is_name_range(const std::string& first, const std::string& last) {
    NumberedName a;
    NumberedName b;
    return split_numbered_name(first, a) && split_numbered_name(last, b)
        && a.prefix == b.prefix && a.suffix == b.suffix;
}

// Splits what follows "- frame" into items: quoted strings, bare words, and ranges
// "FIRST..LAST" of indexes (0..7) or names (run_01..run_08, "a b_1".."a b_4"). A bare word
// with ".." that is no range, such as "../a.png", is a path. False on a broken quote.
bool split_frame_items(const std::string& text, std::vector<FrameItem>& items) {
    size_t pos = 0;
    while (true) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size()) {
            return true;
        }
        FrameItem item;
        std::string error;
        if (text[pos] == '"') {
            item.quoted = true;
            if (!parse_quoted(text, pos, item.first, error)) {
                return false;
            }
            if (text.compare(pos, 2, "..") == 0) {
                pos += 2;
                if (pos >= text.size() || text[pos] != '"' || !parse_quoted(text, pos, item.last, error)) {
                    return false;
                }
                item.is_range = true;
            }
        } else {
            const size_t end = std::min(text.size(), text.find_first_of(" \t", pos));
            const std::string word = text.substr(pos, end - pos);
            pos = end;
            const size_t dots = word.find("..");
            item.first = word;
            if (dots != std::string::npos && dots > 0 && dots + 2 < word.size()) {
                const std::string a = word.substr(0, dots);
                const std::string b = word.substr(dots + 2);
                int ia = 0;
                int ib = 0;
                if ((parse_int(a, ia) && parse_int(b, ib)) || is_name_range(a, b)) {
                    item.first = a;
                    item.last = b;
                    item.is_range = true;
                }
            }
        }
        items.push_back(std::move(item));
    }
}

// Adds the sprite indexes an item names to `frames`, reporting what names no sprite.
void add_frame_item(const FrameItem& item, const std::string& anim_name, const std::string& root,
                    int sprite_count, const std::unordered_map<std::string, int>& by_path,
                    const std::unordered_map<std::string, int>& by_name, std::vector<int>& frames,
                    std::vector<std::string>& warnings) {
    const std::string count = std::to_string(sprite_count);
    const auto add_index = [&](int idx, const std::string& text) {
        if (idx >= 0 && idx < sprite_count) {
            frames.push_back(idx);
        } else {
            warnings.push_back(sprat::core::tr_format(
                "Animation \"{0}\" frame {1} is out of range (the layout has {2} sprites); it is ignored",
                {anim_name, text, count}));
        }
    };
    const auto add_name = [&](const std::string& name) {
        const int idx = resolve_sprite_index(root, name, by_path, by_name);
        if (idx >= 0) {
            frames.push_back(idx);
        } else {
            warnings.push_back(sprat::core::tr_format(
                "Animation \"{0}\" frame \"{1}\" matches no sprite; it is ignored", {anim_name, name}));
        }
    };

    if (!item.is_range) {
        int idx = -1;
        // A bare number is a sprite index; quote the path to name a sprite such as "1.png".
        if (!item.quoted && parse_int(item.first, idx)) {
            add_index(idx, item.first);
        } else {
            add_name(item.first);
        }
        return;
    }

    const std::string range = item.first + ".." + item.last;
    int a = 0;
    int b = 0;
    const bool by_index = !item.quoted && parse_int(item.first, a) && parse_int(item.last, b);
    NumberedName first;
    NumberedName last;
    if (!by_index) {
        if (!is_name_range(item.first, item.last)) {
            warnings.push_back(sprat::core::tr_format(
                "Animation \"{0}\" range \"{1}\" must have ends that differ only in their number; it is ignored",
                {anim_name, range}));
            return;
        }
        split_numbered_name(item.first, first);
        split_numbered_name(item.last, last);
        a = std::stoi(first.digits);
        b = std::stoi(last.digits);
    }
    if (std::llabs(static_cast<long long>(b) - a) >= k_max_frame_range) {
        warnings.push_back(sprat::core::tr_format(
            "Animation \"{0}\" range \"{1}\" is too long; it is ignored", {anim_name, range}));
        return;
    }
    // Names keep the first end's zero padding: run_01..run_10 lists run_01, run_02, ...
    const size_t width = (!by_index && first.digits.size() > 1 && first.digits[0] == '0')
        ? first.digits.size() : 0;
    const int step = a <= b ? 1 : -1;
    for (int n = a;; n += step) {
        if (by_index) {
            add_index(n, std::to_string(n));
        } else {
            std::string digits = std::to_string(n);
            if (digits.size() < width) {
                digits.insert(0, width - digits.size(), '0');
            }
            add_name(first.prefix + digits + first.suffix);
        }
        if (n == b) {
            break;
        }
    }
}

std::vector<AnimationItem> parse_animations_data(
    const std::string& animations_text,
    bool from_layout_stream,
    const std::unordered_map<std::string, int>& by_path,
    const std::unordered_map<std::string, int>& by_name,
    int sprite_count,
    int& animation_fps_out,
    std::string& animation_loop_out,
    std::string& error,
    std::vector<std::string>& warnings
) {
    const std::string limit = std::to_string(k_max_animation_fps);
    std::vector<AnimationItem> animations;
    // Whether each animation set its own fps or loop mode. The file's "fps" and "loop"
    // lines are defaults for the others wherever they appear, so they are applied last.
    std::vector<bool> own_fps;
    std::vector<bool> own_loop;
    std::unordered_map<std::string, size_t> index_by_name;
    std::istringstream iss(animations_text);
    std::string line;
    AnimationItem* current_anim = nullptr;
    std::string raw_root;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::string keyword = sprat::core::metadata_line_keyword(trimmed);
        if (!is_animations_keyword(keyword)) {
            if (!from_layout_stream || !(is_layout_keyword(keyword) || is_metadata_keyword(keyword))) {
                warnings.push_back(sprat::core::tr_format(
                    "Animations file line \"{0}\" is not a root, fps, loop, animation or - frame line; it is ignored",
                    {trimmed}));
            }
            continue;
        }

        std::istringstream liss(trimmed);
        std::string cmd;
        liss >> cmd;
        std::string extra;

        if (keyword == "root") {
            read_root_line(trimmed, raw_root, "Animations file", warnings);
        } else if (keyword == "fps") {
            std::string fps_token;
            int fps = 0;
            bool partial = false;
            if (!(liss >> fps_token)) {
                warnings.push_back(tr("Animations file fps has no value; it is ignored"));
            } else if (liss >> extra) {
                warnings.push_back(sprat::core::tr_format(
                    "Animations file line \"{0}\" has more than one fps; it is ignored", {trimmed}));
            } else if (!read_animation_fps(fps_token, fps, partial)) {
                warnings.push_back(sprat::core::tr_format(
                    "Animations file fps \"{0}\" is not a number; it is ignored", {fps_token}));
            } else if (fps > k_max_animation_fps) {
                error = sprat::core::tr_format(
                    "Animations file fps {0} is above the limit of {1}", {fps_token, limit});
                return {};
            } else {
                if (partial) {
                    warnings.push_back(sprat::core::tr_format(
                        "Animations file fps \"{0}\" is read as {1}",
                        {fps_token, std::to_string(fps)}));
                }
                animation_fps_out = fps;
            }
        } else if (keyword == "loop") {
            std::string mode;
            if (!(liss >> mode)) {
                warnings.push_back(tr("Animations file loop has no value; it is ignored"));
            } else if (liss >> extra) {
                warnings.push_back(sprat::core::tr_format(
                    "Animations file line \"{0}\" has more than one loop mode; it is ignored", {trimmed}));
            } else if (!is_animation_loop_mode(mode)) {
                warnings.push_back(sprat::core::tr_format(
                    "Animations file loop \"{0}\" is not one of {1}; it is ignored",
                    {mode, k_animation_loop_modes}));
            } else {
                animation_loop_out = mode;
            }
        } else if (keyword == "animation") {
            current_anim = nullptr;
            std::string name;
            size_t pos = 9;
            if (!read_name_at(trimmed, pos, name)) {
                warnings.push_back(sprat::core::tr_format(
                    "Animation line \"{0}\" has no valid name; the animation is ignored", {trimmed}));
                continue;
            }
            if (index_by_name.contains(name)) {
                error = sprat::core::tr_format("Animation \"{0}\" is defined more than once", {name});
                return {};
            }

            AnimationItem item;
            item.index = animations.size();
            item.name = name;
            bool has_fps = false;   // a token after the name was taken as the fps
            bool sets_fps = false;  // ... and it gave a usable fps (above 0)
            bool has_loop = false;

            std::istringstream rest(trimmed.substr(pos));
            std::string next_token;
            rest >> next_token;

            if (next_token == "alias") {
                size_t alias_src_pos = trimmed.find("alias", pos) + 5;
                std::string alias_source;
                if (!read_name_at(trimmed, alias_src_pos, alias_source)) {
                    warnings.push_back(sprat::core::tr_format(
                        "Animation \"{0}\" is an alias with no valid source; it is ignored", {name}));
                    continue;
                }
                item.alias_source = alias_source;
                std::string tok;
                std::istringstream flip_rest(trimmed.substr(alias_src_pos));
                while (flip_rest >> tok) {
                    std::string val;
                    if (tok != "flip" || !item.flip.empty()) {
                        warnings.push_back(sprat::core::tr_format(
                            "Animation \"{0}\" has an unknown setting \"{1}\"; it is ignored",
                            {name, tok}));
                    } else if (!(flip_rest >> val)) {
                        warnings.push_back(sprat::core::tr_format(
                            "Animation \"{0}\" flip has no value; it is ignored", {name}));
                    } else if (std::string flip = normalize_flip(val); !flip.empty()) {
                        item.flip = flip;
                    } else {
                        warnings.push_back(sprat::core::tr_format(
                            "Animation \"{0}\" flip \"{1}\" is not one of h, v, hv; it is ignored",
                            {name, val}));
                    }
                }
            } else {
                // After the name come an optional fps and an optional loop mode, in any order.
                for (std::string token = next_token; !token.empty();
                     token.clear(), rest >> token) {
                    if (!has_loop && is_animation_loop_mode(token)) {
                        item.loop = token;
                        has_loop = true;
                        continue;
                    }
                    if (has_fps) {
                        warnings.push_back(sprat::core::tr_format(
                            "Animation \"{0}\" has an unknown setting \"{1}\"; it is ignored",
                            {item.name, token}));
                        continue;
                    }
                    int custom_fps = 0;
                    bool partial = false;
                    has_fps = true;
                    if (read_animation_fps(token, custom_fps, partial)) {
                        if (custom_fps > k_max_animation_fps) {
                            error = sprat::core::tr_format(
                                "Animation \"{0}\" has fps {1}, above the limit of {2}",
                                {item.name, token, limit});
                            return {};
                        }
                        if (partial) {
                            warnings.push_back(sprat::core::tr_format(
                                "Animation \"{0}\" fps \"{1}\" is read as {2}",
                                {item.name, token, std::to_string(custom_fps)}));
                        }
                        item.fps = custom_fps;
                        sets_fps = custom_fps > 0;
                    } else {
                        warnings.push_back(sprat::core::tr_format(
                            "Animation \"{0}\" fps \"{1}\" is not a number; it is ignored",
                            {item.name, token}));
                    }
                }
            }
            const bool is_alias = !item.alias_source.empty();
            index_by_name.emplace(name, animations.size());
            animations.push_back(std::move(item));
            own_fps.push_back(sets_fps);
            own_loop.push_back(has_loop);
            if (!is_alias) {
                current_anim = &animations.back();
            }
        } else {  // "- frame"
            if (!current_anim) {
                warnings.push_back(sprat::core::tr_format(
                    "Frame line \"{0}\" is not inside an animation (or follows an alias, which has no frames of its own); it is ignored",
                    {trimmed}));
                continue;
            }
            std::vector<FrameItem> items;
            const size_t pos = trimmed.find("frame") + 5;
            if (!split_frame_items(trimmed.substr(pos), items)) {
                warnings.push_back(sprat::core::tr_format(
                    "Frame line \"{0}\" has a broken quote or range; it is ignored", {trimmed}));
                continue;
            }
            if (items.empty()) {
                warnings.push_back(sprat::core::tr_format(
                    "Frame line \"{0}\" names no frame; it is ignored", {trimmed}));
                continue;
            }
            for (const FrameItem& item : items) {
                add_frame_item(item, current_anim->name, raw_root, sprite_count, by_path, by_name,
                               current_anim->sprite_indexes, warnings);
            }
        }
    }

    for (size_t i = 0; i < animations.size(); ++i) {
        AnimationItem& anim = animations[i];
        if (!own_fps[i]) {
            anim.fps = animation_fps_out > 0 ? animation_fps_out : DEFAULT_ANIMATION_FPS;
        }
        if (!own_loop[i]) {
            anim.loop = animation_loop_out;
        }
        if (anim.alias_source.empty() && anim.sprite_indexes.empty()) {
            warnings.push_back(sprat::core::tr_format("Animation \"{0}\" has no frames", {anim.name}));
        }
        // An alias must lead, maybe through other aliases, to a regular animation.
        const AnimationItem* at = &anim;
        for (size_t steps = 0; !at->alias_source.empty(); ++steps) {
            const auto it = index_by_name.find(at->alias_source);
            if (it == index_by_name.end()) {
                error = sprat::core::tr_format("Animation \"{0}\" is an alias of unknown animation \"{1}\"",
                                               {at->name, at->alias_source});
                return {};
            }
            if (steps >= animations.size()) {
                error = sprat::core::tr_format("Animation \"{0}\" is part of a cycle of aliases", {anim.name});
                return {};
            }
            at = &animations[it->second];
        }
    }
    return animations;
}

// An alias replays its source, so it plays the way its source does. Chains of aliases are
// followed to the regular animation at their end, whose index becomes source_index; an
// unknown source or a cycle leaves -1 and the alias's own mode (transforms that need the
// source, such as raw, report those).
void resolve_alias_sources(std::vector<AnimationItem>& animations) {
    std::unordered_map<std::string, size_t> by_name;
    for (size_t i = 0; i < animations.size(); ++i) {
        by_name.emplace(animations[i].name, i);
    }
    for (size_t i = 0; i < animations.size(); ++i) {
        size_t at = i;
        for (size_t steps = 0; !animations[at].alias_source.empty() && steps <= animations.size(); ++steps) {
            const auto it = by_name.find(animations[at].alias_source);
            if (it == by_name.end()) {
                break;
            }
            at = it->second;
        }
        if (animations[at].alias_source.empty()) {
            animations[i].source_index = static_cast<int>(at);
            animations[i].loop = animations[at].loop;
        }
    }
}

// ---- Hand-written puppets file -----------------------------------------------------------
//
// A puppet is a cutout rig: parts drawn in file order (first at the back), each with
// interchangeable variants (sprites or animations), joined by "attach" lines that pin a
// point marker of the child part's variant to a marker of the parent part's variant. The
// parent marker's shape is where the child's point may go (a point, or anywhere in a
// circle, rectangle or polygon); leeway lets it drift that many pixels further, and angle
// limits how far the child turns relative to the parent. A part's first attach is its
// parent, so parents form a tree; further attach lines are constraints, which an editor
// (or an engine that wants live physics) honors while posing, and which may close loops.
// A part with no parent is placed by its own position (at:X,Y) in the puppet's space, whose
// origin is the puppet's pivot; all its attach lines are constraints.
//
// A pose sets some parts: a variant, an angle relative to the parent (or around its own
// pivot for a positioned part), an offset within its joint's allowed area, or a position.
// Poses only name what they change, so they layer (walk + smile). sprat describes the rig
// and its poses; engines play them.

// Most degrees a joint's angle range, or a pose's angle, may reach either way.
constexpr int k_max_joint_angle = 360;

struct PuppetVariant {
    std::string name;
    bool is_animation = false;
    int index = -1;  // sprite index, or animation index when is_animation
};

struct PuppetPart {
    std::string name;
    int parent = -1;  // index of the part it is attached to first, or -1
    int joint = -1;   // index of the joint that attaches it to its parent, or -1
    std::vector<PuppetVariant> variants;
    // Where a part with no parent goes: its sprite's pivot at x,y in the puppet's space,
    // whose origin is the puppet's pivot (original-image pixels, Y down).
    bool has_position = false;
    int x = 0;
    int y = 0;
};

struct PuppetJoint {
    size_t child = 0;
    std::string child_marker;
    size_t parent = 0;
    std::string parent_marker;
    bool is_constraint = false;  // a further attach of its child, not its parent
    int leeway = 0;
    bool has_angle = false;
    int angle_min = 0;
    int angle_max = 0;
};

// What a pose sets for one part; each value only when its has_ flag is set.
struct PoseSetting {
    size_t part = 0;
    int variant = -1;  // index into the part's variants, or -1 to keep it
    bool has_angle = false;
    int angle = 0;
    bool has_offset = false;
    int offset_x = 0;
    int offset_y = 0;
    bool has_position = false;
    int x = 0;
    int y = 0;
};

struct PuppetPose {
    std::string name;
    std::vector<PoseSetting> settings;  // in part order
};

struct PuppetItem {
    std::string name;
    std::vector<PuppetPart> parts;
    std::vector<PuppetJoint> joints;
    std::vector<PuppetPose> poses;
};

// Reads `PART:MARKER` (either name quoted or bare) starting at `pos`.
bool read_part_marker(const std::string& line, size_t& pos, std::string& part, std::string& marker) {
    if (!read_name_at(line, pos, part, " \t:") || pos >= line.size() || line[pos] != ':') {
        return false;
    }
    ++pos;
    if (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        return false;  // "PART: MARKER" is a typo, not a marker named after the space
    }
    return read_name_at(line, pos, marker);
}

// Reads the "MIN..MAX" of an angle: setting, in degrees, MIN no greater than MAX.
bool parse_angle_range(const std::string& value, int& min, int& max) {
    const size_t dots = value.find("..");
    return dots != std::string::npos && parse_int(value.substr(0, dots), min)
        && parse_int(value.substr(dots + 2), max) && min <= max && min >= -k_max_joint_angle
        && max <= k_max_joint_angle;
}

// Splits "KEY:VALUE" settings, where a value may be quoted to hold spaces
// (variant:"run back"). A word without a colon has an empty key. False on a broken quote.
bool split_settings(const std::string& text, std::vector<std::pair<std::string, std::string>>& out) {
    size_t pos = 0;
    while (true) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size()) {
            return true;
        }
        const size_t end = std::min(text.size(), text.find_first_of(" \t:", pos));
        if (end >= text.size() || text[end] != ':') {
            out.emplace_back("", text.substr(pos, end - pos));
            pos = end;
            continue;
        }
        std::string key = text.substr(pos, end - pos);
        pos = end + 1;
        std::string value;
        if (pos < text.size() && text[pos] == '"') {
            std::string quote_error;
            if (!parse_quoted(text, pos, value, quote_error)) {
                return false;
            }
        } else {
            const size_t value_end = std::min(text.size(), text.find_first_of(" \t", pos));
            value = text.substr(pos, value_end - pos);
            pos = value_end;
        }
        out.emplace_back(std::move(key), std::move(value));
    }
}

// An attach line before its parts are known: they are looked up when the puppet ends.
struct PendingAttach {
    std::string child;
    std::string parent;
    PuppetJoint joint;
};

// A "- part" line of a pose before its part is known.
struct PendingPosePart {
    std::string line;
    std::string part;
    std::string variant;  // empty to keep it
    PoseSetting setting;
};

struct PendingPose {
    std::string name;
    std::vector<PendingPosePart> parts;
};

// Resolves a puppet's attach lines into parents, joints and constraints, places every
// part, and checks its poses against the rig. False (with `error`) when something names an
// unknown part or variant, attaches a part to itself, attaches two positioned parts,
// closes a cycle of parents, leaves a part unplaced, or poses a part in a way its rig
// doesn't allow.
bool finish_puppet(PuppetItem& puppet, const std::vector<PendingAttach>& attaches,
                   const std::vector<PendingPose>& poses, std::string& error,
                   std::vector<std::string>& warnings) {
    std::unordered_map<std::string, size_t> part_index;
    for (size_t i = 0; i < puppet.parts.size(); ++i) {
        part_index.emplace(puppet.parts[i].name, i);
    }
    for (const PendingAttach& attach : attaches) {
        for (const std::string* name : {&attach.child, &attach.parent}) {
            if (!part_index.contains(*name)) {
                error = sprat::core::tr_format("Puppet \"{0}\" attaches unknown part \"{1}\"",
                                               {puppet.name, *name});
                return false;
            }
        }
        PuppetJoint joint = attach.joint;
        joint.child = part_index[attach.child];
        joint.parent = part_index[attach.parent];
        if (joint.child == joint.parent) {
            error = sprat::core::tr_format("Puppet \"{0}\" attaches part \"{1}\" to itself",
                                           {puppet.name, attach.child});
            return false;
        }
        PuppetPart& child = puppet.parts[joint.child];
        if (child.has_position && puppet.parts[joint.parent].has_position) {
            error = sprat::core::tr_format(
                "Puppet \"{0}\" attaches \"{1}\" to \"{2}\", and both have a position; one of them must be placed by the joint",
                {puppet.name, attach.child, attach.parent});
            return false;
        }
        // The first attach of a part without a position is its parent; the rest constrain it.
        joint.is_constraint = child.has_position || child.parent >= 0;
        if (!joint.is_constraint) {
            child.parent = static_cast<int>(joint.parent);
            child.joint = static_cast<int>(puppet.joints.size());
        }
        puppet.joints.push_back(std::move(joint));
    }
    for (const PuppetPart& part : puppet.parts) {
        // Each part has one parent at most, so walking up either reaches a root or loops.
        const PuppetPart* at = &part;
        for (size_t steps = 0; at->parent >= 0; ++steps) {
            if (steps >= puppet.parts.size()) {
                error = sprat::core::tr_format("Puppet \"{0}\" part \"{1}\" is part of a cycle of attachments",
                                               {puppet.name, part.name});
                return false;
            }
            at = &puppet.parts[static_cast<size_t>(at->parent)];
        }
        if (part.parent < 0 && !part.has_position) {
            error = sprat::core::tr_format(
                "Puppet \"{0}\" part \"{1}\" has no position and is attached to no part; give it at:X,Y or attach it",
                {puppet.name, part.name});
            return false;
        }
        if (part.variants.empty()) {
            warnings.push_back(sprat::core::tr_format("Puppet \"{0}\" part \"{1}\" has no sprites or animations",
                                                      {puppet.name, part.name}));
        }
    }
    if (puppet.parts.empty()) {
        warnings.push_back(sprat::core::tr_format("Puppet \"{0}\" has no parts", {puppet.name}));
    }

    for (const PendingPose& pending : poses) {
        PuppetPose pose;
        pose.name = pending.name;
        std::unordered_set<size_t> posed;
        for (const PendingPosePart& pp : pending.parts) {
            const auto it = part_index.find(pp.part);
            if (it == part_index.end()) {
                error = sprat::core::tr_format("Puppet \"{0}\" pose \"{1}\" sets unknown part \"{2}\"",
                                               {puppet.name, pose.name, pp.part});
                return false;
            }
            if (!posed.insert(it->second).second) {
                error = sprat::core::tr_format("Puppet \"{0}\" pose \"{1}\" sets part \"{2}\" more than once",
                                               {puppet.name, pose.name, pp.part});
                return false;
            }
            const PuppetPart& part = puppet.parts[it->second];
            PoseSetting setting = pp.setting;
            setting.part = it->second;
            if (!pp.variant.empty()) {
                const auto variant = std::find_if(part.variants.begin(), part.variants.end(),
                                                  [&](const PuppetVariant& v) { return v.name == pp.variant; });
                if (variant == part.variants.end()) {
                    error = sprat::core::tr_format("Puppet \"{0}\" pose \"{1}\" part \"{2}\" has no variant \"{3}\"",
                                                   {puppet.name, pose.name, pp.part, pp.variant});
                    return false;
                }
                setting.variant = static_cast<int>(variant - part.variants.begin());
            }
            if (setting.has_position && !part.has_position) {
                error = sprat::core::tr_format(
                    "Puppet \"{0}\" pose \"{1}\" moves part \"{2}\", which its joint places; turn it with angle: or shift it with offset:",
                    {puppet.name, pose.name, pp.part});
                return false;
            }
            if (setting.has_offset && part.has_position) {
                error = sprat::core::tr_format(
                    "Puppet \"{0}\" pose \"{1}\" offsets part \"{2}\", which has no joint to drift in; move it with at:X,Y",
                    {puppet.name, pose.name, pp.part});
                return false;
            }
            if (setting.has_angle && part.joint >= 0) {
                const PuppetJoint& joint = puppet.joints[static_cast<size_t>(part.joint)];
                if (joint.has_angle && (setting.angle < joint.angle_min || setting.angle > joint.angle_max)) {
                    error = sprat::core::tr_format(
                        "Puppet \"{0}\" pose \"{1}\" turns part \"{2}\" {3} degrees, outside its joint's {4}..{5}",
                        {puppet.name, pose.name, pp.part, std::to_string(setting.angle),
                         std::to_string(joint.angle_min), std::to_string(joint.angle_max)});
                    return false;
                }
            }
            pose.settings.push_back(setting);
        }
        if (pose.settings.empty()) {
            warnings.push_back(sprat::core::tr_format("Puppet \"{0}\" pose \"{1}\" sets no part",
                                                      {puppet.name, pose.name}));
        }
        std::ranges::sort(pose.settings, {}, &PoseSetting::part);
        puppet.poses.push_back(std::move(pose));
    }
    return true;
}

// Reads a "- sprite" or "- animation" line into a variant: what it shows, then an optional
// `-> "NAME"`. False, with a warning, when it is not that or shows nothing known.
bool read_variant_line(const std::string& trimmed, const std::string& keyword, const std::string& root,
                       int sprite_count, const std::unordered_map<std::string, int>& by_path,
                       const std::unordered_map<std::string, int>& by_name,
                       const std::vector<std::string>& sprite_names,
                       const std::unordered_map<std::string, size_t>& animation_index,
                       PuppetVariant& out, std::vector<std::string>& warnings) {
    out.is_animation = keyword == "- animation";
    const std::string_view word = out.is_animation ? "animation" : "sprite";
    size_t pos = trimmed.find(word) + word.size();
    bool quoted = false;
    std::string target;
    std::string name;
    bool ok = read_name_at(trimmed, pos, target, " \t", &quoted);
    if (ok) {
        // After what it shows comes nothing, or "-> NAME".
        const std::string tail = trim_copy(trimmed.substr(pos));
        size_t name_pos = 2;
        ok = tail.empty()
            || (tail.starts_with("->") && read_name_at(tail, name_pos, name)
                && trim_copy(tail.substr(name_pos)).empty());
    }
    if (!ok) {
        warnings.push_back(sprat::core::tr_format(
            out.is_animation
                ? "Puppets file line \"{0}\" is not - animation \"ANIMATION\" [-> \"NAME\"]; it is ignored"
                : "Puppets file line \"{0}\" is not - sprite \"SPRITE\" [-> \"NAME\"]; it is ignored",
            {trimmed}));
        return false;
    }
    if (out.is_animation) {
        const auto it = animation_index.find(target);
        if (it == animation_index.end()) {
            warnings.push_back(sprat::core::tr_format(
                "Puppets file animation \"{0}\" matches no animation; it is ignored", {target}));
            return false;
        }
        out.index = static_cast<int>(it->second);
    } else {
        int idx = -1;
        // As in "- frame": a bare number is a sprite index; quote a path such as "1.png".
        if (!quoted && parse_int(target, idx)) {
            if (idx < 0 || idx >= sprite_count) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file sprite {0} is out of range (the layout has {1} sprites); it is ignored",
                    {target, std::to_string(sprite_count)}));
                return false;
            }
        } else {
            idx = resolve_sprite_index(root, target, by_path, by_name);
            if (idx < 0) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file sprite \"{0}\" matches no sprite; it is ignored", {target}));
                return false;
            }
        }
        out.index = idx;
    }
    out.name = !name.empty() ? name
        : out.is_animation ? target
        : sprite_names[static_cast<size_t>(out.index)];
    return true;
}

// Reads the settings after an attach line's parts: leeway:N and angle:MIN..MAX, in any
// order, each at most once. What doesn't fit is reported and ignored.
void read_attach_settings(const std::string& settings, const std::string& line, PuppetJoint& joint,
                          std::vector<std::string>& warnings) {
    std::istringstream iss(settings);
    std::string token;
    bool has_leeway = false;
    while (iss >> token) {
        const size_t colon = token.find(':');
        const std::string key = colon == std::string::npos ? "" : token.substr(0, colon);
        const std::string value = colon == std::string::npos ? "" : token.substr(colon + 1);
        int leeway = 0;
        int angle_min = 0;
        int angle_max = 0;
        if (key == "leeway" && !has_leeway) {
            if (parse_int(value, leeway) && leeway >= 0) {
                joint.leeway = leeway;
                has_leeway = true;
            } else {
                warnings.push_back(sprat::core::tr_format(
                    "Attach line \"{0}\" leeway \"{1}\" is not a whole number of pixels, 0 or more; it is ignored",
                    {line, value}));
            }
        } else if (key == "angle" && !joint.has_angle) {
            if (parse_angle_range(value, angle_min, angle_max)) {
                joint.angle_min = angle_min;
                joint.angle_max = angle_max;
                joint.has_angle = true;
            } else {
                warnings.push_back(sprat::core::tr_format(
                    "Attach line \"{0}\" angle \"{1}\" is not MIN..MAX degrees between -{2} and {2}; it is ignored",
                    {line, value, std::to_string(k_max_joint_angle)}));
            }
        } else {
            warnings.push_back(sprat::core::tr_format(
                "Attach line \"{0}\" has an unknown or repeated setting \"{1}\"; it is ignored",
                {line, token}));
        }
    }
}

// Reads the settings after a part's name: at:X,Y, at most once. What doesn't fit is
// reported and ignored.
void read_part_settings(const std::string& settings, const std::string& line, PuppetPart& part,
                        std::vector<std::string>& warnings) {
    std::istringstream iss(settings);
    std::string token;
    while (iss >> token) {
        int x = 0;
        int y = 0;
        if (token.starts_with("at:") && !part.has_position) {
            if (parse_pair(token.substr(3), x, y)) {
                part.has_position = true;
                part.x = x;
                part.y = y;
            } else {
                warnings.push_back(sprat::core::tr_format(
                    "Part line \"{0}\" position \"{1}\" is not at:X,Y in whole pixels; it is ignored",
                    {line, token.substr(3)}));
            }
        } else {
            warnings.push_back(sprat::core::tr_format(
                "Part line \"{0}\" has an unknown or repeated setting \"{1}\"; it is ignored",
                {line, token}));
        }
    }
}

// Reads a pose's "- part" line: the part, then variant:NAME, angle:DEGREES, offset:X,Y and
// at:X,Y, in any order, each at most once. False, with a warning, when it names no part;
// a setting that doesn't fit is reported and ignored.
bool read_pose_part_line(const std::string& trimmed, PendingPosePart& out, std::vector<std::string>& warnings) {
    out.line = trimmed;
    size_t pos = trimmed.find("part") + 4;
    std::vector<std::pair<std::string, std::string>> settings;
    if (!read_name_at(trimmed, pos, out.part) || !split_settings(trimmed.substr(pos), settings)) {
        warnings.push_back(sprat::core::tr_format(
            "Puppets file line \"{0}\" is not - part \"PART\" [variant:NAME] [angle:DEGREES] [offset:X,Y] [at:X,Y]; it is ignored",
            {trimmed}));
        return false;
    }
    PoseSetting& s = out.setting;
    for (const auto& [key, value] : settings) {
        const auto bad = [&](const char* expected) {
            warnings.push_back(sprat::core::tr_format(
                "Pose line \"{0}\" {1} \"{2}\" is not {3}; it is ignored", {trimmed, key, value, expected}));
        };
        if (key == "variant" && out.variant.empty()) {
            if (value.empty()) {
                bad("a variant name");
            } else {
                out.variant = value;
            }
        } else if (key == "angle" && !s.has_angle) {
            if (parse_int(value, s.angle) && s.angle >= -k_max_joint_angle && s.angle <= k_max_joint_angle) {
                s.has_angle = true;
            } else {
                s.angle = 0;
                bad("whole degrees between -360 and 360");
            }
        } else if (key == "offset" && !s.has_offset) {
            s.has_offset = parse_pair(value, s.offset_x, s.offset_y);
            if (!s.has_offset) {
                s.offset_x = s.offset_y = 0;
                bad("X,Y in whole pixels");
            }
        } else if (key == "at" && !s.has_position) {
            s.has_position = parse_pair(value, s.x, s.y);
            if (!s.has_position) {
                s.x = s.y = 0;
                bad("X,Y in whole pixels");
            }
        } else {
            warnings.push_back(sprat::core::tr_format(
                "Pose line \"{0}\" has an unknown or repeated setting \"{1}\"; it is ignored",
                {trimmed, key.empty() ? value : key + ":" + value}));
        }
    }
    if (out.variant.empty() && !s.has_angle && !s.has_offset && !s.has_position) {
        warnings.push_back(sprat::core::tr_format("Pose line \"{0}\" sets nothing; it is ignored", {trimmed}));
        return false;
    }
    return true;
}

std::vector<PuppetItem> parse_puppets_data(const std::string& puppets_text, bool from_layout_stream,
                                           const std::unordered_map<std::string, int>& by_path,
                                           const std::unordered_map<std::string, int>& by_name,
                                           const std::vector<std::string>& sprite_names,
                                           const std::vector<AnimationItem>& animations,
                                           std::string& error, std::vector<std::string>& warnings) {
    std::unordered_map<std::string, size_t> animation_index;
    for (size_t i = 0; i < animations.size(); ++i) {
        animation_index.emplace(animations[i].name, i);
    }
    const int sprite_count = static_cast<int>(sprite_names.size());
    std::vector<PuppetItem> puppets;
    std::unordered_set<std::string> puppet_names;
    std::vector<PendingAttach> attaches;  // of the current puppet
    std::vector<PendingPose> poses;       // of the current puppet
    std::unordered_set<std::string> pose_names;
    PuppetPart* current_part = nullptr;
    PendingPose* current_pose = nullptr;
    std::unordered_set<std::string> part_names;
    std::unordered_set<std::string> variant_names;
    // Set after a part line that couldn't be read: its variants are dropped with it.
    bool skipping_part = false;
    std::string raw_root;

    const auto end_puppet = [&]() {
        if (!puppets.empty() && !finish_puppet(puppets.back(), attaches, poses, error, warnings)) {
            return false;
        }
        attaches.clear();
        poses.clear();
        pose_names.clear();
        return true;
    };

    std::istringstream iss(puppets_text);
    std::string line;
    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::string keyword = sprat::core::metadata_line_keyword(trimmed);
        if (!is_puppets_keyword(keyword)) {
            if (!from_layout_stream || !(is_layout_keyword(keyword) || is_metadata_keyword(keyword))) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not a root, puppet, part, - sprite, - animation, attach, pose or - part line; it is ignored",
                    {trimmed}));
            }
            continue;
        }
        if (keyword == "root") {
            read_root_line(trimmed, raw_root, "Puppets file", warnings);
            continue;
        }
        if (keyword == "puppet") {
            if (!end_puppet()) {
                return {};
            }
            current_part = nullptr;
            current_pose = nullptr;
            skipping_part = false;
            std::string name;
            size_t pos = keyword.size();
            if (!read_name_at(trimmed, pos, name) || !trim_copy(trimmed.substr(pos)).empty()) {
                error = sprat::core::tr_format("Puppets file line \"{0}\" is not puppet \"NAME\"", {trimmed});
                return {};
            }
            if (!puppet_names.insert(name).second) {
                error = sprat::core::tr_format("Puppet \"{0}\" is defined more than once", {name});
                return {};
            }
            puppets.push_back({name, {}, {}, {}});
            part_names.clear();
            continue;
        }
        if (puppets.empty()) {
            warnings.push_back(sprat::core::tr_format(
                "Puppets file line \"{0}\" is not inside a puppet; it is ignored", {trimmed}));
            continue;
        }
        PuppetItem& puppet = puppets.back();
        if (keyword != "- part") {
            current_pose = nullptr;  // a pose's "- part" lines follow it directly
        }
        if (keyword == "pose") {
            current_part = nullptr;
            skipping_part = false;
            std::string name;
            size_t pos = keyword.size();
            if (!read_name_at(trimmed, pos, name) || !trim_copy(trimmed.substr(pos)).empty()) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not pose \"NAME\"; the pose is ignored", {trimmed}));
                continue;
            }
            if (!pose_names.insert(name).second) {
                error = sprat::core::tr_format("Puppet \"{0}\" has more than one pose \"{1}\"",
                                               {puppet.name, name});
                return {};
            }
            poses.push_back({name, {}});
            current_pose = &poses.back();
        } else if (keyword == "- part") {
            if (current_pose == nullptr) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not inside a pose; it is ignored", {trimmed}));
                continue;
            }
            PendingPosePart pose_part;
            if (read_pose_part_line(trimmed, pose_part, warnings)) {
                current_pose->parts.push_back(std::move(pose_part));
            }
        } else if (keyword == "part") {
            current_part = nullptr;
            PuppetPart part;
            size_t pos = keyword.size();
            skipping_part = !read_name_at(trimmed, pos, part.name);
            if (skipping_part) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not part \"NAME\" [at:X,Y]; the part is ignored",
                    {trimmed}));
                continue;
            }
            if (!part_names.insert(part.name).second) {
                error = sprat::core::tr_format("Puppet \"{0}\" has more than one part \"{1}\"",
                                               {puppet.name, part.name});
                return {};
            }
            read_part_settings(trimmed.substr(pos), trimmed, part, warnings);
            puppet.parts.push_back(std::move(part));
            current_part = &puppet.parts.back();
            variant_names.clear();
        } else if (keyword == "attach") {
            PendingAttach attach;
            size_t pos = keyword.size();
            std::string to;
            bool ok = read_part_marker(trimmed, pos, attach.child, attach.joint.child_marker);
            ok = ok && read_name_at(trimmed, pos, to) && to == "to"
                && read_part_marker(trimmed, pos, attach.parent, attach.joint.parent_marker);
            if (!ok) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not attach \"PART\":\"MARKER\" to \"PART\":\"MARKER\"; it is ignored",
                    {trimmed}));
                continue;
            }
            read_attach_settings(trimmed.substr(pos), trimmed, attach.joint, warnings);
            attaches.push_back(std::move(attach));
        } else {  // "- sprite" or "- animation"
            if (skipping_part) {
                continue;  // already reported with the part
            }
            if (current_part == nullptr) {
                warnings.push_back(sprat::core::tr_format(
                    "Puppets file line \"{0}\" is not inside a part; it is ignored", {trimmed}));
                continue;
            }
            PuppetVariant variant;
            if (!read_variant_line(trimmed, keyword, raw_root, sprite_count, by_path, by_name,
                                   sprite_names, animation_index, variant, warnings)) {
                continue;
            }
            if (!variant_names.insert(variant.name).second) {
                error = sprat::core::tr_format("Puppet \"{0}\" part \"{1}\" has more than one variant \"{2}\"",
                                               {puppet.name, current_part->name, variant.name});
                return {};
            }
            current_part->variants.push_back(std::move(variant));
        }
    }
    if (!end_puppet()) {
        return {};
    }
    return puppets;
}

// The sprites a variant shows: its sprite, or every frame of its animation (of the
// animation an alias replays).
std::vector<int> variant_sprites(const PuppetVariant& variant, const std::vector<AnimationItem>& animations) {
    if (!variant.is_animation) {
        return {variant.index};
    }
    const int source = animations[static_cast<size_t>(variant.index)].source_index;
    return source < 0 ? std::vector<int>{} : animations[static_cast<size_t>(source)].sprite_indexes;
}

// Reports every sprite of a joined part that lacks the joint's marker: the child's must be a
// point, the parent's any shape. Global markers count for every sprite.
void check_puppet_markers(const std::vector<PuppetItem>& puppets, const std::vector<AnimationItem>& animations,
                          const std::vector<MarkerItem>& marker_items,
                          const std::vector<std::vector<MarkerItem>>& sprite_markers,
                          const std::vector<std::string>& sprite_names, std::vector<std::string>& warnings) {
    std::unordered_map<std::string, const MarkerItem*> global_markers;
    for (const MarkerItem& m : marker_items) {
        if (m.sprite_index < 0) {
            global_markers.emplace(m.name, &m);
        }
    }
    const auto find_marker = [&](int sprite, const std::string& name) -> const MarkerItem* {
        for (const MarkerItem& m : sprite_markers[static_cast<size_t>(sprite)]) {
            if (m.name == name) {
                return &m;
            }
        }
        const auto it = global_markers.find(name);
        return it == global_markers.end() ? nullptr : it->second;
    };
    for (const PuppetItem& puppet : puppets) {
        // Two joints may need the same marker of the same sprite; that is said once.
        std::unordered_set<std::string> said;
        for (const PuppetJoint& joint : puppet.joints) {
            const std::string& child_name = puppet.parts[joint.child].name;
            const auto check_end = [&](const PuppetPart& part, const std::string& marker_name, bool is_child) {
                std::unordered_set<int> reported;
                for (const PuppetVariant& variant : part.variants) {
                    for (int sprite : variant_sprites(variant, animations)) {
                        const MarkerItem* marker = find_marker(sprite, marker_name);
                        if ((marker != nullptr && (!is_child || marker->type == "point"))
                            || !reported.insert(sprite).second) {
                            continue;
                        }
                        std::string warning = sprat::core::tr_format(
                            is_child
                                ? "Puppet \"{0}\" part \"{1}\" sprite \"{2}\" has no point marker \"{3}\" to attach it by"
                                : "Puppet \"{0}\" part \"{1}\" sprite \"{2}\" has no marker \"{3}\" to attach part \"{4}\" to",
                            {puppet.name, part.name, sprite_names[static_cast<size_t>(sprite)], marker_name,
                             child_name});
                        if (said.insert(warning).second) {
                            warnings.push_back(std::move(warning));
                        }
                    }
                }
            };
            check_end(puppet.parts[joint.child], joint.child_marker, true);
            check_end(puppet.parts[joint.parent], joint.parent_marker, false);
        }
    }
}

// Part positions and joint leeways are in original-image pixels, like markers. Fails when one, once scaled to
// atlas pixels, would exceed sprat::core::k_max_coordinate.
bool check_puppet_limits(const std::vector<PuppetItem>& puppets, double scale, std::string& error) {
    const auto fits = [&](int v) {
        return sprat::core::within_coordinate_limit(sprat::core::scale_to_atlas(v, scale));
    };
    for (const PuppetItem& puppet : puppets) {
        for (const PuppetPose& pose : puppet.poses) {
            for (const PoseSetting& set : pose.settings) {
                if (!fits(set.x) || !fits(set.y) || !fits(set.offset_x) || !fits(set.offset_y)) {
                    error = sprat::core::tr_format(
                        "Puppet \"{0}\" pose \"{1}\" places part \"{2}\" beyond {3} atlas pixels",
                        {puppet.name, pose.name, puppet.parts[set.part].name,
                         std::to_string(sprat::core::k_max_coordinate)});
                    return false;
                }
            }
        }
        for (const PuppetPart& part : puppet.parts) {
            if (!fits(part.x) || !fits(part.y)) {
                error = sprat::core::tr_format("Puppet \"{0}\" part \"{1}\" has a position beyond {2} atlas pixels",
                                               {puppet.name, part.name,
                                                std::to_string(sprat::core::k_max_coordinate)});
                return false;
            }
        }
        for (const PuppetJoint& joint : puppet.joints) {
            if (!sprat::core::within_coordinate_limit(sprat::core::scale_to_atlas(joint.leeway, scale))) {
                error = sprat::core::tr_format("Puppet \"{0}\" part \"{1}\" has a leeway beyond {2} atlas pixels",
                                               {puppet.name, puppet.parts[joint.child].name,
                                                std::to_string(sprat::core::k_max_coordinate)});
                return false;
            }
        }
    }
    return true;
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

// Name for the whole atlas set, e.g. a Phaser texture key. A single atlas uses its own stem;
// multipack atlases share one key: the pattern's stem without its %d placeholder
// ("atlas_%d.png" -> "atlas").
std::string texture_key(const std::string& pattern, size_t atlas_count, const std::string& atlas_stem) {
    if (atlas_count <= 1 || pattern.empty()) {
        return atlas_stem;
    }
    const std::string stem = fs::path(pattern).stem().string();
    std::string key;
    for (size_t i = 0; i < stem.size(); ++i) {
        if (stem[i] == '%' && i + 1 < stem.size()) {
            if (stem[i + 1] == '%') key.push_back('%');
            ++i;
            continue;
        }
        key.push_back(stem[i]);
    }
    const auto is_separator = [](char c) { return c == '_' || c == '-' || c == '.' || c == ' '; };
    while (!key.empty() && is_separator(key.back())) key.pop_back();
    while (!key.empty() && is_separator(key.front())) key.erase(key.begin());
    return key.empty() ? "atlas" : key;
}

// Build the JSON data string passed as std.extVar("sprat") to all transforms.
std::string build_sprat_json(
    const Layout& layout,
    const std::vector<std::string>& sprite_names,
    const std::vector<MarkerItem>& marker_items,
    const std::vector<AnimationItem>& normalized_animations,
    const std::vector<PuppetItem>& puppets,
    const std::vector<std::vector<MarkerItem>>& sprite_markers,
    int global_pivot_x,
    int global_pivot_y,
    bool has_global_pivot,
    const std::string& output_pattern_arg,
    const std::string& image_pattern_arg,
    const std::string& output_stem,
    const std::string& markers_path_arg,
    const std::string& animations_path_arg,
    const std::string& puppets_path_arg,
    int animation_fps)
{
    // How the metadata refers to atlas image N: --image-path when given, else the -a path.
    // The -a path itself still names the per-atlas output files and seeds stable hashes.
    const std::string& reference_pattern = image_pattern_arg.empty() ? output_pattern_arg : image_pattern_arg;
    auto image_path = [&](int atlas_index) { return format_atlas_path(reference_pattern, atlas_index); };

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
    // Marker files use original-image pixels; exported markers use atlas pixels.
    auto marker_to_json = [&](const MarkerItem& m) -> std::string {
        const auto sc = [&](int v) { return std::to_string(sprat::core::scale_to_atlas(v, layout.scale)); };
        std::string o = "{\"name\":\"" + escape_json(m.name) + "\"";
        o += ",\"type\":\"" + escape_json(m.type) + "\"";
        o += ",\"x\":" + sc(m.x);
        o += ",\"y\":" + sc(m.y);
        if (m.type == "circle") {
            o += ",\"radius\":" + sc(m.radius);
        } else if (m.type == "rectangle") {
            o += ",\"w\":" + sc(m.w);
            o += ",\"h\":" + sc(m.h);
        } else if (m.type == "polygon") {
            o += ",\"vertices\":[";
            for (size_t vi = 0; vi < m.vertices.size(); ++vi) {
                if (vi > 0) o += ',';
                o += "{\"x\":" + sc(m.vertices[vi].first);
                o += ",\"y\":" + sc(m.vertices[vi].second) + "}";
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
        const sprat::core::MetadataTrim trim = sprat::core::metadata_trim(s, layout.scale);
        const int source_w  = content_w + trim.left + trim.right;
        const int source_h  = content_h + trim.top + trim.bottom;
        const bool has_trim = (trim.left != 0) || (trim.top != 0) ||
                              (trim.right != 0) || (trim.bottom != 0);

        int unity_y = 0;
        if (s.atlas_index >= 0 && static_cast<size_t>(s.atlas_index) < layout.atlases.size()) {
            unity_y = layout.atlases[static_cast<size_t>(s.atlas_index)].height - s.y - s.h;
        }

        bool has_pivot = has_global_pivot;
        int pivot_src_x = has_global_pivot ? global_pivot_x : 0;
        int pivot_src_y = has_global_pivot ? global_pivot_y : 0;
        for (const auto& marker : sprite_markers[i]) {
            if (marker.name == "pivot" && marker.type == "point") {
                pivot_src_x = marker.x;
                pivot_src_y = marker.y;
                has_pivot = true;
                break;
            }
        }
        // Pivot markers are in original-image pixels; normalize from the unrounded scaled value.
        const double pivot_ax = static_cast<double>(pivot_src_x) * layout.scale;
        const double pivot_ay = static_cast<double>(pivot_src_y) * layout.scale;
        const int px = sprat::core::scale_to_atlas(pivot_src_x, layout.scale);
        const int py = sprat::core::scale_to_atlas(pivot_src_y, layout.scale);

        double pivot_x_norm = (source_w > 0) ? (pivot_ax / source_w) : 0.0;
        double pivot_y_norm = (source_h > 0) ? (1.0 - pivot_ay / source_h) : 0.0;
        double pivot_y_norm_raw = (source_h > 0) ? (pivot_ay / source_h) : 0.0;
        // Relative to the trimmed rect, for engines whose sprite rect has no trim (Unity).
        const double pivot_rect_x_norm = (content_w > 0) ? ((pivot_ax - trim.left) / content_w) : 0.0;
        const double pivot_rect_y_norm_raw = (content_h > 0) ? ((pivot_ay - trim.top) / content_h) : 0.0;

        const uint64_t nh = sprat::core::fnv1a_hash(
            reinterpret_cast<const unsigned char*>(sname.c_str()), sname.size());
        const std::string nh_hex = to_hex16(nh);
        const std::string nh_dec = std::to_string(nh);
        // Unity stores internalID/fileID as signed 64-bit; reinterpret the hash bits.
        const std::string nh_signed = std::to_string(static_cast<int64_t>(nh));

        const std::string a_path = image_path(s.atlas_index);

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
        o += ",\"trim_left\":" + std::to_string(trim.left);
        o += ",\"trim_top\":" + std::to_string(trim.top);
        o += ",\"trim_right\":" + std::to_string(trim.right);
        o += ",\"trim_bottom\":" + std::to_string(trim.bottom);
        o += ",\"has_trim\":" + std::string(has_trim ? "true" : "false");
        o += ",\"rotated\":" + std::string(s.rotated ? "true" : "false");
        o += ",\"alias_of\":\"" + escape_json(s.alias_of) + "\"";
        o += ",\"has_slice\":" + std::string(s.has_slice ? "true" : "false");
        if (s.has_slice) {
            const sprat::core::MetadataSlice slice =
                sprat::core::metadata_slice(s, layout.scale, source_w, source_h);
            o += ",\"slice_left\":" + std::to_string(slice.left);
            o += ",\"slice_top\":" + std::to_string(slice.top);
            o += ",\"slice_right\":" + std::to_string(slice.right);
            o += ",\"slice_bottom\":" + std::to_string(slice.bottom);
            o += ",\"slice_h\":\"" + s.slice_h + "\"";
            o += ",\"slice_v\":\"" + s.slice_v + "\"";
        }
        o += ",\"content_w\":" + std::to_string(content_w);
        o += ",\"content_h\":" + std::to_string(content_h);
        o += ",\"source_w\":" + std::to_string(source_w);
        o += ",\"source_h\":" + std::to_string(source_h);
        o += ",\"unity_y\":" + std::to_string(unity_y);
        o += ",\"has_pivot\":" + std::string(has_pivot ? "true" : "false");
        o += ",\"pivot_x\":" + std::to_string(px);
        o += ",\"pivot_y\":" + std::to_string(py);
        o += ",\"pivot_x_norm\":" + format_double(pivot_x_norm);
        o += ",\"pivot_y_norm\":" + format_double(pivot_y_norm);
        o += ",\"pivot_y_norm_raw\":" + format_double(pivot_y_norm_raw);
        o += ",\"pivot_rect_x_norm\":" + format_double(pivot_rect_x_norm);
        o += ",\"pivot_rect_y_norm\":" + format_double(1.0 - pivot_rect_y_norm_raw);
        o += ",\"name_hash_hex\":\"" + nh_hex + "\"";
        o += ",\"name_hash_decimal\":\"" + nh_dec + "\"";
        o += ",\"name_hash_signed\":\"" + nh_signed + "\"";
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
    // Same for every transform of a run (a group, or separate runs with the same --atlas),
    // unlike output_stem, which is each transform's own variant name: IDs that one output
    // refers to in another (Unity texture GUIDs) are built from it.
    const uint64_t pattern_hash = sprat::core::fnv1a_hash(
        reinterpret_cast<const unsigned char*>(output_pattern_arg.c_str()), output_pattern_arg.size());

    const std::string atlas_path_0 = image_path(0);
    const std::string atlas_stem_0 = fs::path(format_atlas_path(output_pattern_arg, 0)).stem().string();

    const int eff_fps = animation_fps > 0 ? animation_fps : DEFAULT_ANIMATION_FPS;
    const int atlas_w0 = layout.atlases.empty() ? 0 : layout.atlases[0].width;
    const int atlas_h0 = layout.atlases.empty() ? 0 : layout.atlases[0].height;

    std::string j = "{";

    // Global scalars
    j += "\"atlas_path\":\"" + escape_json(atlas_path_0) + "\"";
    j += ",\"atlas_stem\":\"" + escape_json(atlas_stem_0) + "\"";
    j += ",\"texture_key\":\"" + escape_json(texture_key(output_pattern_arg, layout.atlases.size(), atlas_stem_0)) + "\"";
    j += ",\"atlas_width\":" + std::to_string(atlas_w0);
    j += ",\"atlas_height\":" + std::to_string(atlas_h0);
    j += ",\"atlas_count\":" + std::to_string(layout.atlases.size());
    j += ",\"multipack\":" + std::string(layout.multipack ? "true" : "false");
    j += ",\"scale\":" + format_double(layout.scale);
    // The same value as text: Jsonnet turns numbers into strings with 17 digits (0.1 becomes
    // "0.10000000000000001"), so transforms writing the scale as text use this instead.
    j += ",\"scale_text\":\"" + format_double(layout.scale) + "\"";
    j += ",\"extrude\":" + std::to_string(layout.extrude);
    const bool has_rotated = std::ranges::any_of(layout.sprites, [](const Sprite& s) { return s.rotated; });
    j += ",\"rotation_direction\":\"" + std::string(layout.rotate_ccw ? "ccw" : "cw") + "\"";
    j += ",\"has_rotated\":" + std::string(has_rotated ? "true" : "false");
    j += ",\"sprite_count\":" + std::to_string(layout.sprites.size());
    j += ",\"animation_count\":" + std::to_string(normalized_animations.size());
    j += ",\"marker_count\":" + std::to_string(marker_items.size());
    j += ",\"output_pattern\":\"" + escape_json(output_pattern_arg) + "\"";
    j += ",\"output_stem\":\"" + escape_json(output_stem) + "\"";
    j += ",\"output_stem_hash_hex\":\"" + stem_hash_hex + "\"";
    j += ",\"output_pattern_hash_hex\":\"" + to_hex16(pattern_hash) + "\"";
    j += ",\"has_animations\":" + std::string(normalized_animations.empty() ? "false" : "true");
    j += ",\"has_markers\":" + std::string(marker_items.empty() ? "false" : "true");
    j += ",\"animations_path\":\"" + escape_json(animations_path_arg) + "\"";
    j += ",\"markers_path\":\"" + escape_json(markers_path_arg) + "\"";
    j += ",\"has_puppets\":" + std::string(puppets.empty() ? "false" : "true");
    j += ",\"puppet_count\":" + std::to_string(puppets.size());
    j += ",\"puppets_path\":\"" + escape_json(puppets_path_arg) + "\"";
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
        j += ",\"path\":\"" + escape_json(image_path(static_cast<int>(ai))) + "\"";
        // Per-atlas names and a stable hash, for formats that write one file per atlas.
        const fs::path a_fs_path(a_path);
        j += ",\"stem\":\"" + escape_json(a_fs_path.stem().string()) + "\"";
        j += ",\"filename\":\"" + escape_json(a_fs_path.filename().string()) + "\"";
        const uint64_t a_hash = sprat::core::fnv1a_hash(
            reinterpret_cast<const unsigned char*>(a_path.c_str()), a_path.size());
        j += ",\"hash_hex\":\"" + to_hex16(a_hash) + "\"";
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
        j += ",\"loop\":\"" + anim.loop + "\"";
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
            j += ",\"name_hash_signed\":\"" + std::to_string(static_cast<int64_t>(fnh)) + "\"";
            j += "}";
        }
        j += "]";

        j += "}";
    }
    j += "]";

    // puppets array: parts in draw order (back first), each with its variants, then the
    // joints that attach them. Leeways are scaled to atlas pixels, like markers.
    j += ",\"puppets\":[";
    for (size_t pi = 0; pi < puppets.size(); ++pi) {
        if (pi > 0) j += ',';
        const PuppetItem& puppet = puppets[pi];
        j += "{\"index\":" + std::to_string(pi);
        j += ",\"name\":\"" + escape_json(puppet.name) + "\"";
        j += ",\"parts\":[";
        for (size_t ri = 0; ri < puppet.parts.size(); ++ri) {
            if (ri > 0) j += ',';
            const PuppetPart& part = puppet.parts[ri];
            j += "{\"index\":" + std::to_string(ri);
            j += ",\"name\":\"" + escape_json(part.name) + "\"";
            j += ",\"parent\":" + std::to_string(part.parent);
            j += ",\"parent_name\":\""
                + (part.parent < 0 ? std::string() : escape_json(puppet.parts[static_cast<size_t>(part.parent)].name))
                + "\"";
            j += ",\"has_position\":" + std::string(part.has_position ? "true" : "false");
            j += ",\"x\":" + std::to_string(sprat::core::scale_to_atlas(part.x, layout.scale));
            j += ",\"y\":" + std::to_string(sprat::core::scale_to_atlas(part.y, layout.scale));
            j += ",\"variants\":[";
            for (size_t vi = 0; vi < part.variants.size(); ++vi) {
                if (vi > 0) j += ',';
                const PuppetVariant& variant = part.variants[vi];
                j += "{\"index\":" + std::to_string(vi);
                j += ",\"name\":\"" + escape_json(variant.name) + "\"";
                j += ",\"kind\":\"" + std::string(variant.is_animation ? "animation" : "sprite") + "\"";
                j += ",\"sprite_index\":" + std::to_string(variant.is_animation ? -1 : variant.index);
                j += ",\"animation_index\":" + std::to_string(variant.is_animation ? variant.index : -1);
                j += "}";
            }
            j += "]}";
        }
        j += "],\"joints\":[";
        for (size_t ji = 0; ji < puppet.joints.size(); ++ji) {
            if (ji > 0) j += ',';
            const PuppetJoint& joint = puppet.joints[ji];
            j += "{\"index\":" + std::to_string(ji);
            j += ",\"child\":" + std::to_string(joint.child);
            j += ",\"child_name\":\"" + escape_json(puppet.parts[joint.child].name) + "\"";
            j += ",\"child_marker\":\"" + escape_json(joint.child_marker) + "\"";
            j += ",\"parent\":" + std::to_string(joint.parent);
            j += ",\"parent_name\":\"" + escape_json(puppet.parts[joint.parent].name) + "\"";
            j += ",\"parent_marker\":\"" + escape_json(joint.parent_marker) + "\"";
            j += ",\"is_constraint\":" + std::string(joint.is_constraint ? "true" : "false");
            j += ",\"leeway\":" + std::to_string(sprat::core::scale_to_atlas(joint.leeway, layout.scale));
            j += ",\"has_angle\":" + std::string(joint.has_angle ? "true" : "false");
            j += ",\"angle_min\":" + std::to_string(joint.angle_min);
            j += ",\"angle_max\":" + std::to_string(joint.angle_max);
            j += "}";
        }
        // Poses: what each sets for some parts, in part order; offsets and positions in atlas pixels.
        j += "],\"poses\":[";
        for (size_t oi = 0; oi < puppet.poses.size(); ++oi) {
            if (oi > 0) j += ',';
            const PuppetPose& pose = puppet.poses[oi];
            j += "{\"index\":" + std::to_string(oi);
            j += ",\"name\":\"" + escape_json(pose.name) + "\"";
            j += ",\"parts\":[";
            for (size_t si = 0; si < pose.settings.size(); ++si) {
                if (si > 0) j += ',';
                const PoseSetting& set = pose.settings[si];
                const PuppetPart& part = puppet.parts[set.part];
                j += "{\"part\":" + std::to_string(set.part);
                j += ",\"part_name\":\"" + escape_json(part.name) + "\"";
                j += ",\"variant\":" + std::to_string(set.variant);
                j += ",\"variant_name\":\""
                    + (set.variant < 0 ? std::string() : escape_json(part.variants[static_cast<size_t>(set.variant)].name))
                    + "\"";
                j += ",\"has_angle\":" + std::string(set.has_angle ? "true" : "false");
                j += ",\"angle\":" + std::to_string(set.angle);
                j += ",\"has_offset\":" + std::string(set.has_offset ? "true" : "false");
                j += ",\"offset_x\":" + std::to_string(sprat::core::scale_to_atlas(set.offset_x, layout.scale));
                j += ",\"offset_y\":" + std::to_string(sprat::core::scale_to_atlas(set.offset_y, layout.scale));
                j += ",\"has_position\":" + std::string(set.has_position ? "true" : "false");
                j += ",\"x\":" + std::to_string(sprat::core::scale_to_atlas(set.x, layout.scale));
                j += ",\"y\":" + std::to_string(sprat::core::scale_to_atlas(set.y, layout.scale));
                j += "}";
            }
            j += "]}";
        }
        j += "]}";
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
        error = tr("Failed to initialize Jsonnet VM");
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
    std::string filename;                       // optional --output-dir name for content
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
// Windows opens a device instead of a file for these names, with any extension ("con.anim",
// "nul.json"), so the output would silently go nowhere.
static bool is_windows_reserved_name(const std::string& name) {
    std::string stem = name.substr(0, name.find('.'));
    std::ranges::transform(stem, stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

// A transform-provided filename must name a file directly inside --output-dir: no
// directories, no parent references and nothing Windows treats as a drive or device path.
// Rules are the same on every platform, so output written on one works on the others.
static bool is_safe_output_filename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    // Windows drops a trailing dot or space, so "a." would overwrite "a".
    if (name.back() == '.' || name.back() == ' ' || is_windows_reserved_name(name)) {
        return false;
    }
    return std::ranges::none_of(name, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
               c == '"' || c == '<' || c == '>' || c == '|';
    });
}

static std::string unsafe_output_filename_error(const std::string& name) {
    return std::string(tr("Transform output filename must be a plain file name that is valid on every "
                          "platform (no folders, no device names such as CON or NUL, no trailing dot "
                          "or space): ")) + name;
}

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
        error = tr("Transform result missing required field: extension");
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
            error = tr("Malformed files array in transform result");
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
                if (!is_safe_output_filename(entry.filename)) {
                    error = unsafe_output_filename_error(entry.filename);
                    return false;
                }
                const bool duplicate = std::ranges::any_of(out.files, [&](const auto& fe) {
                    return fe.filename == entry.filename;
                });
                if (duplicate) {
                    error = tr("Transform produces the same output filename twice: ") + entry.filename;
                    return false;
                }
                out.files.push_back(std::move(entry));
            }
            fp = obj_end;
        }
        return true;
    }

    // No "files" key — check for top-level "content" (single-file mode).
    if (find_json_string_value(json, "content", vs, ve)) {
        out.content = json_unescape_string(json, vs, ve);
        // Optional name for --output-dir (stdout output ignores it), e.g. "atlas.png.meta".
        if (find_json_string_value(json, "filename", vs, ve)) {
            out.filename = json_unescape_string(json, vs, ve);
            if (!out.filename.empty() && !is_safe_output_filename(out.filename)) {
                error = unsafe_output_filename_error(out.filename);
                return false;
            }
        }
        return true;
    }

    error = tr("Transform result must have either 'content' or 'files' field");
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

// The data of an empty layout, which transforms are evaluated with to read their name and
// description for listing. Built like real data, so it has every field they may read.
std::string empty_sprat_json() {
    return build_sprat_json(Layout{}, {}, {}, {}, {}, {}, 0, 0, false, "", "", "", "", "", "", -1);
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

    const std::string mock_json = empty_sprat_json();

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

    const std::string mock_json = empty_sprat_json();

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
              << tr("  --image-path PATTERN       Image path written into the metadata (default: the --atlas path as given)\n")
              << tr("  --output-dir PATH          Write output to PATH/{variant}{extension} instead of stdout\n")
              << tr("  --list-transforms          Print available transforms and exit\n")
              << tr("  --list-transforms-json     Print available transforms as JSON and exit\n")
              << tr("  --transforms-dir           Print the transforms directory and exit\n")
              << tr("  --markers PATH             Load external markers file\n")
              << tr("  --animations PATH          Load external animations file (fps up to 100)\n")
              << tr("                             loop modes: loop, once, pingpong, once-pingpong\n")
              << tr("  --auto-animations          Group frames into animations by name pattern\n")
              << tr("  --puppets PATH             Load external puppets file (cutout rigs and their poses)\n")
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
    std::string puppets_path_arg;
    std::string output_pattern_arg;
    std::string image_pattern_arg;
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
        } else if (arg == "--image-path" && i + 1 < argc) {
            image_pattern_arg = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir_arg = argv[++i];
        } else if (arg == "--markers" && i + 1 < argc) {
            markers_path_arg = argv[++i];
        } else if (arg == "--animations" && i + 1 < argc) {
            animations_path_arg = argv[++i];
        } else if (arg == "--puppets" && i + 1 < argc) {
            puppets_path_arg = argv[++i];
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
    if (!parse_layout(layout_iss, layout, layout_error)
        || !sprat::core::expand_layout_aliases(layout, layout_error)) {
        std::cerr << layout_error << "\n";
        return 1;
    }
    for (const std::string& warning : layout.warnings) {
        std::cerr << tr("Warning: ") << warning << "\n";
    }

    if (!image_pattern_arg.empty()) {
        std::string image_pattern_error;
        if (!validate_output_pattern(image_pattern_arg, layout.atlases.size(), true, image_pattern_error)) {
            std::cerr << tr("Invalid image path pattern: ") << image_pattern_error << "\n";
            return 1;
        }
    }
    if (output_pattern_arg.empty()) {
        // Without -a, the --image-path pattern also names the atlas files.
        output_pattern_arg = image_pattern_arg;
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

    // Each metadata file, or the layout stream itself when it isn't given.
    std::string markers_file;
    std::string animations_file;
    std::string puppets_file;
    for (auto [path, text] : {std::pair{&markers_path_arg, &markers_file},
                              std::pair{&animations_path_arg, &animations_file},
                              std::pair{&puppets_path_arg, &puppets_file}}) {
        std::string file_error;
        if (!path->empty() && !read_text_file(fs::path(*path), *text, file_error)) {
            std::cerr << file_error << "\n";
            return 1;
        }
    }
    const std::string& markers_text = markers_path_arg.empty() ? input_text : markers_file;
    const std::string& animations_text = animations_path_arg.empty() ? input_text : animations_file;
    const std::string& puppets_text = puppets_path_arg.empty() ? input_text : puppets_file;

    std::vector<std::vector<MarkerItem>> sprite_markers;
    std::vector<std::string> markers_warnings;
    const std::vector<MarkerItem> marker_items =
        parse_markers_data(markers_text, markers_path_arg.empty(), layout, sprite_index_by_path,
                           sprite_index_by_name,
                           sprite_names, sprite_markers, markers_warnings);
    for (const std::string& warning : markers_warnings) {
        std::cerr << tr("Warning: ") << warning << "\n";
    }
    for (const MarkerItem& marker : marker_items) {
        std::string marker_error;
        if (!check_marker_limits(marker, layout.scale, marker_error)) {
            std::cerr << marker_error << "\n";
            return 1;
        }
    }
    int animation_fps = -1;
    std::string animation_loop = DEFAULT_ANIMATION_LOOP;
    std::string animations_error;
    std::vector<std::string> animations_warnings;
    std::vector<AnimationItem> animation_items =
        parse_animations_data(animations_text, animations_path_arg.empty(), sprite_index_by_path,
                              sprite_index_by_name,
                               static_cast<int>(layout.sprites.size()), animation_fps, animation_loop, animations_error,
                               animations_warnings);
    for (const std::string& warning : animations_warnings) {
        std::cerr << tr("Warning: ") << warning << "\n";
    }
    if (!animations_error.empty()) {
        std::cerr << animations_error << "\n";
        return 1;
    }

    if (auto_animations) {
        // An animation from the animations file replaces the automatic one of the same name.
        std::unordered_set<std::string> defined_names;
        for (const AnimationItem& item : animation_items) {
            defined_names.insert(item.name);
        }
        std::map<std::string, std::vector<int>> grouped;
        for (size_t i = 0; i < sprite_names.size(); ++i) {
            std::string anim_name = get_animation_name(sprite_names[i]);
            if (anim_name.empty()) continue;
            grouped[anim_name].push_back(static_cast<int>(i));
        }
        for (auto& [name, frames] : grouped) {
            if (frames.size() > 1 && !defined_names.contains(name)) {
                // Layout order is packing order (and aliases come last), so order
                // frames by name: run_2 before run_10.
                // Stable, so names natural order can't tell apart ("run_02", "run_2") keep
                // layout order on every platform.
                std::stable_sort(frames.begin(), frames.end(), [&](int a, int b) {
                    return sprat::core::compare_natural(sprite_names[static_cast<size_t>(a)],
                                                        sprite_names[static_cast<size_t>(b)]) < 0;
                });
                AnimationItem anim;
                anim.index = animation_items.size();
                anim.name = name;
                anim.sprite_indexes = frames;
                anim.fps = (animation_fps > 0 ? animation_fps : DEFAULT_ANIMATION_FPS);
                anim.loop = animation_loop;
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
    resolve_alias_sources(normalized_animations);

    // Puppets come last: their variants name animations, the automatic ones included.
    std::string puppets_error;
    std::vector<std::string> puppets_warnings;
    const std::vector<PuppetItem> puppets =
        parse_puppets_data(puppets_text, puppets_path_arg.empty(), sprite_index_by_path,
                           sprite_index_by_name, sprite_names, normalized_animations,
                           puppets_error, puppets_warnings);
    // After an error the list is empty, so these checks find nothing to report.
    check_puppet_markers(puppets, normalized_animations, marker_items, sprite_markers,
                         sprite_names, puppets_warnings);
    for (const std::string& warning : puppets_warnings) {
        std::cerr << tr("Warning: ") << warning << "\n";
    }
    if (!puppets_error.empty() || !check_puppet_limits(puppets, layout.scale, puppets_error)) {
        std::cerr << puppets_error << "\n";
        return 1;
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
                std::string write_error;
                if (!sprat::core::write_file(out_path, fe.content, write_error)) {
                    std::cerr << tr("Failed to write output file ") << to_quoted(out_path.string())
                              << ": " << write_error << "\n";
                    exit_code = 1;
                }
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
            const std::string out_filename =
                result.filename.empty() ? file_stem + result.extension : result.filename;
            const fs::path out_path = fs::path(out_dir) / out_filename;
            std::string write_error;
            if (!sprat::core::write_file(out_path, result.content, write_error)) {
                std::cerr << tr("Failed to write output file ") << to_quoted(out_path.string())
                          << ": " << write_error << "\n";
                return 1;
            }
            return 0;
        }

        if (stdout_out) {
            *stdout_out << result.content;
        }
        return 0;
    };

    if (group_mode) {
        // Evaluate every member before writing anything, so a member that refuses the
        // layout (for example rotated sprites in unity.meta) leaves no partial output.
        std::vector<std::pair<std::string, TransformResult>> results;
        int exit_code = 0;
        for (const GroupMember& member : group_members) {
            const std::string sprat_json = build_sprat_json(
                layout, sprite_names, marker_items, normalized_animations, puppets, sprite_markers,
                global_pivot_x, global_pivot_y, has_global_pivot,
                output_pattern_arg, image_pattern_arg, member.variant,
                markers_path_arg, animations_path_arg, puppets_path_arg, animation_fps);

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
            results.emplace_back(member.variant, std::move(result));
        }
        if (exit_code != 0) {
            std::cerr << tr("No files were written.\n");
            return exit_code;
        }

        std::error_code ec;
        fs::create_directories(output_dir_arg, ec);
        if (ec) {
            std::cerr << tr("Failed to create output directory: ") << ec.message() << "\n";
            return 1;
        }
        for (const auto& [variant, result] : results) {
            const int r = write_result(result, output_dir_arg, variant, nullptr);
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
        layout, sprite_names, marker_items, normalized_animations, puppets, sprite_markers,
        global_pivot_x, global_pivot_y, has_global_pivot,
        output_pattern_arg, image_pattern_arg, output_stem,
        markers_path_arg, animations_path_arg, puppets_path_arg, animation_fps);

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

    const int status = write_result(result, output_dir_arg, output_stem, &std::cout);
    if (status == 0 && output_dir_arg.empty() && !sprat::core::finish_stdout()) {
        return 1;
    }
    return status;
}
