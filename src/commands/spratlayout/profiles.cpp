#include "commands/spratlayout/profiles.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>

#include "core/cli_parse.h"
#include "core/i18n.h"

#ifndef SPRAT_GLOBAL_PROFILE_CONFIG
#define SPRAT_GLOBAL_PROFILE_CONFIG "/usr/local/share/sprat/spratprofiles.cfg"
#endif

namespace sprat::layout {

using sprat::core::parse_double;
using sprat::core::parse_non_negative_int;
using sprat::core::parse_pair;
using sprat::core::parse_positive_int;
using sprat::core::parse_positive_uint;
using sprat::core::tr;
using sprat::core::trim_copy;

const char* const k_global_profiles_config_path = SPRAT_GLOBAL_PROFILE_CONFIG;

namespace {

constexpr const char* k_profiles_config_filename = "spratprofiles.cfg";

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

// Parses a [profile <name>] section header.  On success, initialises `out`
// with default values and the parsed name and returns true.
bool parse_profile_section_header(const std::string& header_content,
                                  std::unordered_set<std::string>& seen_names,
                                  size_t line_number,
                                  ProfileDefinition& out,
                                  std::string& error) {
    std::istringstream iss(header_content);
    std::string section_type;
    if (!(iss >> section_type)) {
        error = tr("empty section header at line ") + std::to_string(line_number);
        return false;
    }
    section_type = to_lower_copy(section_type);
    if (section_type != "profile") {
        error = std::string(tr("unsupported section '")) + section_type + tr("' at line ") + std::to_string(line_number);
        return false;
    }

    std::string remainder;
    std::getline(iss, remainder);
    remainder = trim_copy(remainder);
    if (remainder.empty()) {
        error = tr("missing profile name at line ") + std::to_string(line_number);
        return false;
    }

    std::string name;
    if (remainder.front() == '"') {
        size_t pos = 0;
        std::string parse_error;
        if (!sprat::core::parse_quoted(remainder, pos, name, parse_error)) {
            error = std::string(tr("invalid quoted profile name at line ")) + std::to_string(line_number) + ": " + parse_error;
            return false;
        }
        if (!trim_copy(remainder.substr(pos)).empty()) {
            error = tr("unexpected token after quoted profile name at line ") + std::to_string(line_number);
            return false;
        }
    } else {
        std::istringstream name_iss(remainder);
        std::string extra;
        if (!(name_iss >> name)) {
            error = tr("missing profile name at line ") + std::to_string(line_number);
            return false;
        }
        if (name_iss >> extra) {
            error = std::string(tr("unexpected token '")) + extra + tr("' in profile name (use quotes for names with spaces) at line ") +
                    std::to_string(line_number);
            return false;
        }
    }

    if (seen_names.contains(name)) {
        error = std::string(tr("duplicate profile '")) + name + tr("' at line ") + std::to_string(line_number);
        return false;
    }
    seen_names.insert(name);
    out = ProfileDefinition{};
    out.name = name;
    out.mode = Mode::COMPACT;
    out.optimize_target = OptimizeTarget::GPU;
    return true;
}

// Profiles can also hold the settings of the later pipeline steps (sprat-gui saves its
// profiles this way). spratlayout accepts these keys and leaves them to those tools.
bool is_other_tool_profile_key(const std::string& lower_key) {
    static const char* const keys[] = {
        // spratpack
        "dilate", "gpu_compress", "image_format", "image_quality", "zopfli",
        "frame_lines", "frame_line_width", "frame_line_color", "atlas_index",
        // spratconvert
        "auto_animations",
    };
    return std::ranges::find(keys, lower_key) != std::end(keys);
}

// Applies a single key=value entry to `def`.  Returns false and sets `error`
// on any validation failure.
bool apply_profile_entry(ProfileDefinition& def,
                         const std::string& key,
                         const std::string& value,
                         size_t line_number,
                         std::string& error) {
    const std::string lower_key = to_lower_copy(key);
    if (lower_key == "mode") {
        Mode parsed_mode;
        if (!parse_mode_from_string(value, parsed_mode, error)) {
            error += " at line " + std::to_string(line_number);
            return false;
        }
        def.mode = parsed_mode;
    } else if (lower_key == "preset") {
        PresetDefinition preset;
        if (!parse_preset_from_string(value, preset)) {
            error = std::string(tr("invalid preset '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.mode = preset.mode;
        def.optimize_target = preset.optimize_target;
    } else if (lower_key == "sort") {
        FrameSort frame_sort;
        if (!parse_frame_sort_from_string(value, frame_sort, def.stable_metric)) {
            error = std::string(tr("invalid sort '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.frame_sort = frame_sort;
    } else if (is_other_tool_profile_key(lower_key)) {
        // Read by spratpack/spratconvert front ends such as sprat-gui; nothing to lay out.
    } else if (lower_key == "optimize") {
        OptimizeTarget parsed_target;
        if (!parse_optimize_target_from_string(value, parsed_target, error)) {
            error += " at line " + std::to_string(line_number);
            return false;
        }
        def.optimize_target = parsed_target;
    } else if (lower_key == "max_width" || lower_key == "default_max_width") {
        int parsed_width = 0;
        if (!parse_positive_int(value, parsed_width)) {
            error = std::string(tr("invalid max_width '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.max_width = parsed_width;
    } else if (lower_key == "max_height" || lower_key == "default_max_height") {
        int parsed_height = 0;
        if (!parse_positive_int(value, parsed_height)) {
            error = std::string(tr("invalid max_height '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.max_height = parsed_height;
    } else if (lower_key == "padding") {
        int parsed_padding = 0;
        if (!parse_non_negative_int(value, parsed_padding)) {
            error = std::string(tr("invalid padding '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.padding = parsed_padding;
    } else if (lower_key == "extrude") {
        int parsed_extrude = 0;
        if (!parse_non_negative_int(value, parsed_extrude)) {
            error = std::string(tr("invalid extrude '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.extrude = parsed_extrude;
    } else if (lower_key == "scale") {
        double parsed_scale = 0.0;
        if (!parse_scale_factor(value, parsed_scale)) {
            error = std::string(tr("invalid scale '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.scale = parsed_scale;
    } else if (lower_key == "trim_transparent") {
        bool parsed_trim = false;
        if (!parse_bool_value(value, parsed_trim)) {
            error = std::string(tr("invalid trim_transparent '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.trim_transparent = parsed_trim;
    } else if (lower_key == "rotate") {
        bool parsed_rotate = false;
        if (!parse_bool_value(value, parsed_rotate)) {
            error = std::string(tr("invalid rotate '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.rotate = parsed_rotate;
    } else if (lower_key == "rotate_direction") {
        bool parsed_ccw = false;
        if (!parse_rotate_direction(value, parsed_ccw)) {
            error = std::string(tr("invalid rotate_direction '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.rotate_ccw = parsed_ccw;
    } else if (lower_key == "threads") {
        unsigned int parsed_threads = 0;
        if (!parse_positive_uint(value, parsed_threads)) {
            error = std::string(tr("invalid threads '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.threads = parsed_threads;
    } else if (lower_key == "multipack") {
        bool parsed_multipack = false;
        if (!parse_bool_value(value, parsed_multipack)) {
            error = std::string(tr("invalid multipack '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.multipack = parsed_multipack;
    } else if (lower_key == "incremental") {
        bool parsed_incremental = false;
        if (!parse_bool_value(value, parsed_incremental)) {
            error = std::string(tr("invalid incremental '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.incremental = parsed_incremental;
    } else if (lower_key == "source_resolution") {
        int w = 0;
        int h = 0;
        if (!parse_resolution(value, w, h)) {
            error = std::string(tr("invalid source_resolution '")) + value + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        def.source_resolution = std::make_pair(w, h);
    } else if (lower_key == "target_resolution") {
        if (to_lower_copy(value) == "source") {
            def.target_resolution = std::make_pair(-1, -1);
        } else {
            int w = 0;
            int h = 0;
            if (!parse_resolution(value, w, h)) {
                error = std::string(tr("invalid target_resolution '")) + value + tr("' at line ") + std::to_string(line_number);
                return false;
            }
            def.target_resolution = std::make_pair(w, h);
        }
    } else if (lower_key == "resolution_reference") {
        ResolutionReference ref;
        if (!parse_resolution_reference_from_string(value, ref, error)) {
            error += " at line " + std::to_string(line_number);
            return false;
        }
        def.resolution_reference = ref;
    } else if (lower_key == "label") {
        def.label = value;
    } else {
        error = std::string(tr("unknown key '")) + key + tr("' at line ") + std::to_string(line_number);
        return false;
    }
    return true;
}

std::optional<fs::path> resolve_user_profiles_config_path() {
#ifdef _WIN32
    // Windows: %APPDATA%\sprat\spratprofiles.cfg
    static const char* const envs[] = {"APPDATA", "LOCALAPPDATA"};
    for (const char* env : envs) {
        const char* val = std::getenv(env);
        if (val != nullptr && val[0] != '\0') {
            const fs::path cfg = fs::path(val) / "sprat" / k_profiles_config_filename;
            std::error_code ec;
            if (fs::exists(cfg, ec) && !ec) {
                return cfg;
            }
        }
    }
    return std::nullopt;
#elif defined(__APPLE__)
    // macOS: ~/Library/Application Support/sprat/spratprofiles.cfg
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    const fs::path mac_cfg = fs::path(home) / "Library" / "Application Support" / "sprat" / k_profiles_config_filename;
    std::error_code ec_mac;
    if (fs::exists(mac_cfg, ec_mac) && !ec_mac) {
        return mac_cfg;
    }
    return std::nullopt;
#else
    // Linux/other: $XDG_CONFIG_HOME/sprat/spratprofiles.cfg (default ~/.config/sprat/)
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return std::nullopt;
    }
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    const fs::path cfg = (xdg_config_home != nullptr && xdg_config_home[0] != '\0')
        ? fs::path(xdg_config_home) / "sprat" / k_profiles_config_filename
        : fs::path(home) / ".config" / "sprat" / k_profiles_config_filename;
    std::error_code ec;
    if (fs::exists(cfg, ec) && !ec) {
        return cfg;
    }
    return std::nullopt;
#endif
}

} // namespace

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
    if (lower == "grid") {
        out = Mode::GRID;
        return true;
    }
    error = std::string(tr("invalid mode '")) + value + "'";
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
    error = std::string(tr("invalid optimize target '")) + value + "'";
    return false;
}

bool parse_preset_from_string(const std::string& value, PresetDefinition& out) {
    std::string lower = to_lower_copy(value);
    if (lower == "fast") {
        out = {Mode::FAST, OptimizeTarget::GPU};
        return true;
    }
    if (lower == "quality") {
        out = {Mode::COMPACT, OptimizeTarget::GPU};
        return true;
    }
    if (lower == "small") {
        out = {Mode::COMPACT, OptimizeTarget::SPACE};
        return true;
    }
    if (lower == "pot") {
        out = {Mode::POT, OptimizeTarget::GPU};
        return true;
    }
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
    error = std::string(tr("invalid resolution reference '")) + value + "'";
    return false;
}

// "cw" (default) or "ccw": direction of the 90-degree rotation of rotated sprites.
bool parse_rotate_direction(const std::string& value, bool& rotate_ccw) {
    if (value == "cw") {
        rotate_ccw = false;
        return true;
    }
    if (value == "ccw") {
        rotate_ccw = true;
        return true;
    }
    return false;
}

bool parse_scale_factor(const std::string& value, double& out) {
    double parsed = 0.0;
    if (!parse_double(value, parsed) || parsed <= 0.0 || parsed > 1.0) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_resolution(const std::string& value, int& out_width, int& out_height) {
    int width = 0;
    int height = 0;
    if (!parse_pair(value, width, height, 'x') || width <= 0 || height <= 0) {
        return false;
    }
    out_width = width;
    out_height = height;
    return true;
}

bool parse_frame_sort_from_string(const std::string& value, FrameSort& out, StableMetric& out_metric) {
    std::string lower = to_lower_copy(value);
    if (lower == "name") {
        out = FrameSort::Name;
        return true;
    }
    if (lower == "none") {
        out = FrameSort::None;
        return true;
    }
    if (lower == "stable" || lower.starts_with("stable:")) {
        out = FrameSort::Stable;
        const std::string metric_str = lower.size() > 7 ? lower.substr(7) : "area";
        if (metric_str == "area")      { out_metric = StableMetric::Area;      return true; }
        if (metric_str == "maxside")   { out_metric = StableMetric::MaxSide;   return true; }
        if (metric_str == "height")    { out_metric = StableMetric::Height;    return true; }
        if (metric_str == "width")     { out_metric = StableMetric::Width;     return true; }
        if (metric_str == "perimeter") { out_metric = StableMetric::Perimeter; return true; }
        return false;
    }
    return false;
}

namespace {

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
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            if (current) {
                out.push_back(*current);
            }
            current.reset();
            ProfileDefinition def;
            if (!parse_profile_section_header(trimmed.substr(1, trimmed.size() - 2),
                                              seen_names, line_number, def, error)) {
                return false;
            }
            current = def;
            continue;
        }

        if (!current) {
            error = tr("entry outside of profile section at line ") + std::to_string(line_number);
            return false;
        }

        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            error = std::string(tr("invalid line '")) + trimmed + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        const std::string key = trim_copy(trimmed.substr(0, equals));
        const std::string value = trim_copy(trimmed.substr(equals + 1));
        if (key.empty()) {
            error = tr("empty key at line ") + std::to_string(line_number);
            return false;
        }
        if (value.empty()) {
            error = std::string(tr("empty value for key '")) + key + tr("' at line ") + std::to_string(line_number);
            return false;
        }
        if (!apply_profile_entry(*current, key, value, line_number, error)) {
            return false;
        }
    }

    if (current) {
        out.push_back(*current);
    }
    if (out.empty()) {
        error = tr("no profiles defined");
        return false;
    }
    return true;
}

} // namespace

bool load_profiles_config_from_file(const fs::path& path,
                                    std::vector<ProfileDefinition>& out,
                                    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = std::string(tr("failed to open '")) + path.string() + "'";
        return false;
    }
    return parse_profiles_config(input, out, error);
}

std::vector<fs::path> build_default_profiles_config_candidates(const fs::path& exec_dir) {
    std::vector<fs::path> candidates;
    // Lookup order:
    // 1) {exec_dir}/spratprofiles.cfg (beside executable, portable install)
    // 2) user config:
    //      Windows:  %APPDATA%\sprat\spratprofiles.cfg
    //      macOS:    ~/Library/Application Support/sprat/spratprofiles.cfg
    //      Linux:    $XDG_CONFIG_HOME/sprat/spratprofiles.cfg (default ~/.config/sprat/)
    // 3) global installed config
    if (!exec_dir.empty()) {
        candidates.push_back(exec_dir / k_profiles_config_filename);
    }
    if (std::optional<fs::path> user_config = resolve_user_profiles_config_path()) {
        candidates.push_back(*user_config);
    }
    candidates.emplace_back(k_global_profiles_config_path);
    return candidates;
}

} // namespace sprat::layout
