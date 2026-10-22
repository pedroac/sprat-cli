// spratlayout: scan images and write a text layout of their atlas positions.
//
// The pipeline lives in src/commands/spratlayout/: input (folders, lists, archives),
// sprites (measuring and deduplication), packing and search (placing sprites),
// cache (on-disk caches) and output (the layout text). This file parses options,
// resolves profiles and runs the pipeline.

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
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "commands/entrypoints.h"
#include "commands/spratlayout/cache.h"
#include "commands/spratlayout/common.h"
#include "commands/spratlayout/input.h"
#include "commands/spratlayout/output.h"
#include "commands/spratlayout/packing.h"
#include "commands/spratlayout/profiles.h"
#include "commands/spratlayout/search.h"
#include "commands/spratlayout/sprites.h"
#include "core/checked_math.h"
#include "core/cli_parse.h"
#include "core/i18n.h"
#include "core/perceptual_dedup.h"

namespace {

using namespace sprat::layout;
using sprat::core::checked_add_int;
using sprat::core::checked_mul_size_t;
using sprat::core::parse_non_negative_int;
using sprat::core::parse_positive_int;
using sprat::core::parse_positive_uint;
using sprat::core::to_quoted;
using sprat::core::tr;

constexpr const char k_default_profile_name[] = "fast";
constexpr Mode k_default_mode = Mode::FAST;
constexpr OptimizeTarget k_default_optimize_target = OptimizeTarget::GPU;
constexpr int k_default_padding = 0;
constexpr int k_default_extrude = 0;
constexpr double k_default_scale = 1.0;
constexpr bool k_default_trim_transparent = false;
constexpr unsigned int k_default_threads = 0;
// Compact profiles whose layouts a compact run can also cache (see prewarm_compact_profiles).
constexpr std::array<const char*, 3> k_compact_prewarm_profiles = {
    "desktop",
    "mobile",
    "space",
};

bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    std::string lower = to_lower_copy(value);
    return !(lower == "0" || lower == "false" || lower == "no" || lower == "off");
}

void print_usage() {
    std::cout << tr("Usage: spratlayout <folder> [OPTIONS]\n")
              << tr("       spratlayout --stdin-list [OPTIONS]\n")
              << tr("\n")
              << tr("Scan an image folder/list/tar and write a text layout to standard output.\n")
              << tr("Rotated sprites are emitted with a trailing 'rotated' token.\n")
              << tr("\n")
              << tr("Presets (recommended starting point):\n")
              << tr("  --preset fast              Quick shelf packing, GPU-friendly dimensions\n")
              << tr("  --preset quality           Best packing quality, GPU-friendly dimensions\n")
              << tr("  --preset small             Best packing quality, smallest possible area\n")
              << tr("  --preset pot               Power-of-two atlas, GPU-friendly dimensions\n")
              << tr("\n")
              << tr("Options:\n")
              << tr("  --profile NAME             Profile name from config (default: fast)\n")
              << tr("  --profiles-config PATH     Use an explicit profile configuration file\n")
              << tr("  --default-profiles-config  Print the default profiles config path and exit\n")
              << tr("  --mode MODE                Packing algorithm: compact, pot, or fast\n")
              << tr("  --optimize TARGET          Optimization metric: gpu (min max-side) or space (min area)\n")
              << tr("  --max-width N              Maximum atlas width\n")
              << tr("  --max-height N             Maximum atlas height\n")
              << tr("  --no-max-width             Disable width limit (even if profile sets one)\n")
              << tr("  --no-max-height            Disable height limit (even if profile sets one)\n")
              << tr("  --padding N                Extra pixels between packed sprites\n")
              << tr("  --extrude N                Repeat edge pixels N times (padding should be >= extrude * 2)\n")
              << tr("  --source-resolution WxH    Source design resolution baseline\n")
              << tr("  --target-resolution WxH    Target output resolution\n")
              << tr("  --resolution-reference REF Axis ratio driver: largest or smallest\n")
              << tr("  --scale F                  Pre-scale factor (0 < F <= 1)\n")
              << tr("  --trim-transparent         Enable transparent-border trimming\n")
              << tr("  --rotate                   Allow 90-degree sprite rotation during packing\n")
              << tr("  --rotate-direction DIR     Rotation direction of rotated sprites: cw (default) or ccw\n")
              << tr("                             (libGDX needs ccw; TexturePacker-style formats need cw)\n")
              << tr("  --multipack                Split into multiple atlases if they don't fit\n")
              << tr("  --incremental              Reuse positions for unchanged sprites (layout stability;\n")
              << tr("                             single atlas only, ignored in grid mode)\n")
              << tr("  --deduplicate <mode>       Deduplication mode: none, exact, perceptual\n")
              << tr("  --dedup-threshold N        Max Hamming distance for perceptual dedup (0-64, default: 5)\n")
              << tr("  --dedup-tolerance N        Max per-channel pixel difference for perceptual dedup (0-255, default: 8)\n")
              << tr("  --sort name|none|stable[:<metric>]  Order of sprites in layout (default: none)\n")
              << tr("                             stable: deterministic sort by size then path; <metric> is\n")
              << tr("                             area (default), maxside, height, width, or perimeter\n")
              << tr("  --threads N                Number of worker threads\n")
              << tr("  --debug                    Enable detailed error reporting and debug visualization\n")
              << tr("  --stdin-list               Read image paths from stdin (one per line) instead of <folder>\n")
              << tr("  Directory inputs honor .spratlayoutignore; list files may use exclude \"path\"\n")
              << tr("  --help, -h                 Show this help message\n")
              << tr("  --version, -v              Show version\n");
}


// All settings derived from parsing argv.  Fields that express an override
// (has_*_override == true) indicate that the corresponding value was
// explicitly supplied on the command line; profile loading will only fill in
// fields whose override flag is false.
struct LayoutArgs {
    bool debug = false;
    fs::path folder;
    std::string requested_profile_name;
    std::string profiles_config_path;
    // raw override state (preset is resolved into has_mode/optimize_override
    // before try_parse_args returns)
    bool has_mode_override = false;
    Mode mode_override = k_default_mode;
    bool has_optimize_override = false;
    OptimizeTarget optimize_override = k_default_optimize_target;
    int max_width_limit = 0;
    bool has_max_width_limit = false;
    int max_height_limit = 0;
    bool has_max_height_limit = false;
    int padding = 0;
    bool has_padding_override = false;
    int extrude = 0;
    bool has_extrude_override = false;
    int source_resolution_width = 0;
    int source_resolution_height = 0;
    bool has_source_resolution = false;
    int target_resolution_width = 0;
    int target_resolution_height = 0;
    bool has_target_resolution = false;
    ResolutionReference resolution_reference = ResolutionReference::Largest;
    bool has_resolution_reference_override = false;
    double scale = 0.0;
    bool has_scale_override = false;
    bool trim_transparent = false;
    bool has_trim_override = false;
    bool allow_rotate = false;
    bool has_rotate_override = false;
    bool rotate_ccw = false;
    bool has_rotate_direction_override = false;
    bool multipack = false;
    bool has_multipack_override = false;
    std::string deduplicateMode = "none";
    bool has_deduplicate_override = false;
    int dedup_threshold = sprat::core::k_default_dedup_hash_threshold;
    bool has_dedup_threshold_override = false;
    int dedup_tolerance = sprat::core::k_default_dedup_pixel_tolerance;
    FrameSort frame_sort = FrameSort::Name;
    StableMetric stable_metric = StableMetric::Area;
    bool has_frame_sort_override = false;
    unsigned int thread_limit = 0;
    bool has_threads_override = false;
    bool incremental = false;
    bool has_incremental_override = false;
    bool show_profiles_config = false;
    bool stdin_list = false;
};

// Parses argv into args.  Returns -1 to signal the caller should continue, or
// 0/1 as an exit code for --help/--version/error cases.
int try_parse_args(int argc, char** argv, LayoutArgs& args) {
    args.debug = env_flag_enabled("SPRAT_DEBUG");
    bool has_preset = false;
    PresetDefinition preset_definition{Mode::FAST, OptimizeTarget::GPU};

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << tr("spratlayout version ") << SPRAT_VERSION << "\n";
            return 0;
        } else if (arg == "--debug") {
            args.debug = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            args.requested_profile_name = argv[++i];
        } else if (arg == "--preset" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_preset_from_string(value, preset_definition)) {
                std::cerr << tr("Invalid preset: ") << value
                          << tr(". Valid presets: fast, quality, small, pot\n");
                return 1;
            }
            has_preset = true;
        } else if (arg == "--profiles-config" && i + 1 < argc) {
            args.profiles_config_path = argv[++i];
        } else if (arg == "--default-profiles-config") {
            args.show_profiles_config = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::string error;
            if (!parse_mode_from_string(value, args.mode_override, error)) {
                std::cerr << tr("Invalid mode value: ") << value << "\n";
                return 1;
            }
            args.has_mode_override = true;
        } else if (arg == "--optimize" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::string error;
            if (!parse_optimize_target_from_string(value, args.optimize_override, error)) {
                std::cerr << tr("Invalid optimize value: ") << value << "\n";
                return 1;
            }
            args.has_optimize_override = true;
        } else if (arg == "--max-width" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_positive_int(value, args.max_width_limit)) {
                std::cerr << tr("Invalid max width value: ") << value << "\n";
                return 1;
            }
            args.has_max_width_limit = true;
        } else if (arg == "--max-height" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_positive_int(value, args.max_height_limit)) {
                std::cerr << tr("Invalid max height value: ") << value << "\n";
                return 1;
            }
            args.has_max_height_limit = true;
        } else if (arg == "--no-max-width") {
            args.max_width_limit = 0;
            args.has_max_width_limit = true;
        } else if (arg == "--no-max-height") {
            args.max_height_limit = 0;
            args.has_max_height_limit = true;
        } else if (arg == "--padding" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_non_negative_int(value, args.padding)) {
                std::cerr << tr("Invalid padding value: ") << value << "\n";
                return 1;
            }
            args.has_padding_override = true;
        } else if (arg == "--extrude" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_non_negative_int(value, args.extrude)) {
                std::cerr << tr("Invalid extrude value: ") << value << "\n";
                return 1;
            }
            args.has_extrude_override = true;
        } else if (arg == "--source-resolution" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_resolution(value, args.source_resolution_width, args.source_resolution_height)) {
                std::cerr << tr("Invalid source resolution value: ") << value << "\n";
                return 1;
            }
            args.has_source_resolution = true;
        } else if (arg == "--target-resolution" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_resolution(value, args.target_resolution_width, args.target_resolution_height)) {
                std::cerr << tr("Invalid target resolution value: ") << value << "\n";
                return 1;
            }
            args.has_target_resolution = true;
        } else if (arg == "--resolution-reference" && i + 1 < argc) {
            if (args.has_resolution_reference_override) {
                std::cerr << tr("Error: --resolution-reference can only be provided once\n");
                return 1;
            }
            const std::string value = argv[++i];
            std::string error;
            if (!parse_resolution_reference_from_string(value, args.resolution_reference, error)) {
                std::cerr << tr("Invalid resolution reference value: ") << value << "\n";
                return 1;
            }
            args.has_resolution_reference_override = true;
        } else if (arg == "--scale" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_scale_factor(value, args.scale)) {
                std::cerr << tr("Invalid scale value: ") << value << "\n";
                return 1;
            }
            args.has_scale_override = true;
        } else if (arg == "--trim-transparent") {
            args.trim_transparent = true;
            args.has_trim_override = true;
        } else if (arg == "--rotate") {
            args.allow_rotate = true;
            args.has_rotate_override = true;
        } else if (arg == "--multipack") {
            args.multipack = true;
            args.has_multipack_override = true;
        } else if (arg == "--deduplicate" && i + 1 < argc) {
            args.deduplicateMode = argv[++i];
            if (args.deduplicateMode != "none" && args.deduplicateMode != "exact"
                    && args.deduplicateMode != "perceptual") {
                std::cerr << tr("Invalid deduplication mode: ") << args.deduplicateMode << "\n";
                std::cerr << tr("Valid modes: none, exact, perceptual\n");
                return 1;
            }
            args.has_deduplicate_override = true;
        } else if (arg == "--dedup-threshold" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_non_negative_int(value, args.dedup_threshold) || args.dedup_threshold > 64) {
                std::cerr << tr("Invalid dedup threshold: ") << value << tr(" (must be 0-64)\n");
                return 1;
            }
            args.has_dedup_threshold_override = true;
        } else if (arg == "--dedup-tolerance" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_non_negative_int(value, args.dedup_tolerance) || args.dedup_tolerance > 255) {
                std::cerr << tr("Invalid dedup tolerance: ") << value << tr(" (must be 0-255)\n");
                return 1;
            }
        } else if (arg == "--rotate-direction" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_rotate_direction(value, args.rotate_ccw)) {
                std::cerr << tr("Invalid rotate direction: ") << value << tr(" (must be cw or ccw)\n");
                return 1;
            }
            args.has_rotate_direction_override = true;
        } else if (arg == "--sort" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_frame_sort_from_string(value, args.frame_sort, args.stable_metric)) {
                std::cerr << tr("Invalid sort value: ") << value << "\n";
                return 1;
            }
            args.has_frame_sort_override = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (!parse_positive_uint(value, args.thread_limit)) {
                std::cerr << tr("Invalid thread count: ") << value << "\n";
                return 1;
            }
            args.has_threads_override = true;
        } else if (arg == "--incremental") {
            args.incremental = true;
            args.has_incremental_override = true;
        } else if (arg == "--stdin-list") {
            args.stdin_list = true;
        } else if (arg.starts_with("-")) {
            std::cerr << tr("Unknown argument: ") << arg << "\n";
            return 1;
        } else {
            if (args.folder.empty()) {
                args.folder = arg;
            } else {
                std::cerr << tr("Error: Too many arguments: ") << arg << "\n";
                return 1;
            }
        }
    }

    // Apply --preset: fold into the override flags if not already set
    // explicitly by --mode / --optimize.
    if (has_preset) {
        if (!args.has_mode_override) {
            args.mode_override = preset_definition.mode;
            args.has_mode_override = true;
        }
        if (!args.has_optimize_override) {
            args.optimize_override = preset_definition.optimize_target;
            args.has_optimize_override = true;
        }
    }

    return -1; // continue
}

// Effective settings: command-line options, then the selected profile, then defaults.
struct LayoutSettings {
    std::string profile_name = k_default_profile_name;
    Mode mode = k_default_mode;
    OptimizeTarget optimize_target = k_default_optimize_target;
    int max_width_limit = 0;   // 0 = no limit
    int max_height_limit = 0;  // 0 = no limit
    int padding = k_default_padding;
    int extrude = k_default_extrude;
    double scale = k_default_scale;  // includes the resolution scale
    bool trim_transparent = k_default_trim_transparent;
    bool allow_rotate = false;
    bool rotate_ccw = false;
    bool multipack = false;
    unsigned int thread_limit = k_default_threads;
    bool incremental = false;
    // Profiles of the loaded config file, by name.
    std::unordered_map<std::string, ProfileDefinition> profiles;
};

const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::FAST: return "fast";
        case Mode::COMPACT: return "compact";
        case Mode::GRID: return "grid";
        case Mode::POT: return "pot";
    }
    return "pot";
}

// Loads the first readable profiles config: --profiles-config, or the default candidates.
// Fails only when a profile was requested.
bool load_profiles(const LayoutArgs& args, const fs::path& cwd, const fs::path& exec_dir,
                   std::vector<ProfileDefinition>& profile_definitions,
                   bool& loaded_profile_file, fs::path& loaded_profile_path) {
    const bool has_requested_profile = !args.requested_profile_name.empty();
    std::vector<fs::path> config_candidates;
    if (!args.profiles_config_path.empty()) {
        fs::path config_candidate(args.profiles_config_path);
        if (config_candidate.is_relative() && !cwd.empty()) {
            config_candidate = cwd / config_candidate;
        }
        config_candidates.push_back(std::move(config_candidate));
    } else {
        config_candidates = build_default_profiles_config_candidates(exec_dir);
    }

    loaded_profile_file = false;
    std::string config_error;
    std::vector<std::string> tried_candidates;
    std::vector<std::string> candidate_access_errors;
    for (const fs::path& candidate : config_candidates) {
        if (!load_profiles_config_from_file(candidate, profile_definitions, config_error)) {
            if (config_error.rfind("failed to open '", 0) == 0) {
                tried_candidates.push_back(candidate.string());
                candidate_access_errors.push_back(candidate.string() + " (" + config_error + ")");
                continue;
            }
            if (has_requested_profile) {
                std::cerr << tr("Failed to load profile config (") << to_quoted(candidate) << tr("): ") << config_error << "\n";
                return false;
            }
            tried_candidates.push_back(candidate.string());
            continue;
        }
        loaded_profile_file = true;
        loaded_profile_path = candidate;
        break;
    }

    if (has_requested_profile && !loaded_profile_file) {
        std::cerr << tr("Failed to load profile config. Tried:");
        for (const std::string& candidate : tried_candidates) {
            std::cerr << " " << candidate;
        }
        std::cerr << tr("\n");
        if (!candidate_access_errors.empty()) {
            std::cerr << tr("Profile config access errors:\n");
            for (const std::string& error : candidate_access_errors) {
                std::cerr << tr("  - ") << to_quoted(error) << "\n";
            }
        }
        return false;
    }
    return true;
}

// Fills in every setting the command line left unset from the profile.
void apply_profile(const ProfileDefinition& profile, const LayoutArgs& args, LayoutSettings& s,
                   bool& has_source_resolution, int& source_w, int& source_h,
                   bool& has_target_resolution, int& target_w, int& target_h,
                   ResolutionReference& resolution_reference) {
    if (!args.has_mode_override) {
        s.mode = profile.mode;
    }
    if (!args.has_optimize_override) {
        s.optimize_target = profile.optimize_target;
    }
    if (!args.has_max_width_limit && profile.max_width) {
        s.max_width_limit = *profile.max_width;
    }
    if (!args.has_max_height_limit && profile.max_height) {
        s.max_height_limit = *profile.max_height;
    }
    if (!args.has_padding_override && profile.padding) {
        s.padding = *profile.padding;
    }
    if (!args.has_extrude_override && profile.extrude) {
        s.extrude = *profile.extrude;
    }
    if (!args.has_scale_override && profile.scale) {
        s.scale = *profile.scale;
    }
    if (!args.has_trim_override && profile.trim_transparent) {
        s.trim_transparent = *profile.trim_transparent;
    }
    if (!args.has_rotate_override && profile.rotate) {
        s.allow_rotate = *profile.rotate;
    }
    if (!args.has_rotate_direction_override && profile.rotate_ccw) {
        s.rotate_ccw = *profile.rotate_ccw;
    }
    if (!args.has_multipack_override && profile.multipack) {
        s.multipack = *profile.multipack;
    }
    if (!args.has_threads_override && profile.threads) {
        s.thread_limit = *profile.threads;
    }
    if (!args.has_incremental_override && profile.incremental) {
        s.incremental = *profile.incremental;
    }
    if (!has_source_resolution && profile.source_resolution) {
        source_w = profile.source_resolution->first;
        source_h = profile.source_resolution->second;
        has_source_resolution = true;
    }
    if (!has_target_resolution && profile.target_resolution) {
        if (profile.target_resolution->first == -1 && profile.target_resolution->second == -1) {
            if (has_source_resolution) {
                target_w = source_w;
                target_h = source_h;
                has_target_resolution = true;
            }
        } else {
            target_w = profile.target_resolution->first;
            target_h = profile.target_resolution->second;
            has_target_resolution = true;
        }
    }
    if (!args.has_resolution_reference_override && profile.resolution_reference) {
        resolution_reference = *profile.resolution_reference;
    }
}

bool resolve_settings(const LayoutArgs& args, const fs::path& cwd, const fs::path& exec_dir,
                      LayoutSettings& s) {
    s.mode = args.has_mode_override ? args.mode_override : k_default_mode;
    s.optimize_target = args.has_optimize_override ? args.optimize_override : k_default_optimize_target;
    s.max_width_limit = args.max_width_limit;
    s.max_height_limit = args.max_height_limit;
    s.padding = args.has_padding_override ? args.padding : k_default_padding;
    s.extrude = args.has_extrude_override ? args.extrude : k_default_extrude;
    s.scale = args.has_scale_override ? args.scale : k_default_scale;
    s.trim_transparent = args.has_trim_override ? args.trim_transparent : k_default_trim_transparent;
    s.allow_rotate = args.has_rotate_override ? args.allow_rotate : false;
    s.rotate_ccw = args.has_rotate_direction_override ? args.rotate_ccw : false;
    s.multipack = args.has_multipack_override ? args.multipack : false;
    s.thread_limit = args.has_threads_override ? args.thread_limit : k_default_threads;
    s.incremental = args.has_incremental_override ? args.incremental : false;
    int source_w = args.source_resolution_width;
    int source_h = args.source_resolution_height;
    int target_w = args.target_resolution_width;
    int target_h = args.target_resolution_height;
    bool has_source_resolution = args.has_source_resolution;
    bool has_target_resolution = args.has_target_resolution;
    ResolutionReference resolution_reference = args.resolution_reference;

    const bool has_requested_profile = !args.requested_profile_name.empty();
    if (has_requested_profile) {
        s.profile_name = args.requested_profile_name;
    }

    std::vector<ProfileDefinition> profile_definitions;
    bool loaded_profile_file = false;
    fs::path loaded_profile_path;
    if (!load_profiles(args, cwd, exec_dir, profile_definitions, loaded_profile_file, loaded_profile_path)) {
        return false;
    }
    if (loaded_profile_file) {
        for (const auto& def : profile_definitions) {
            s.profiles.emplace(def.name, def);
        }
    }

    // Without --profile, the default profile applies only if the config defines it.
    const bool should_apply_profile =
        has_requested_profile || (loaded_profile_file && s.profiles.contains(s.profile_name));
    const bool profile_debug = env_flag_enabled("SPRAT_PROFILE_DEBUG");
    if (profile_debug) {
        std::cerr << "[profile-debug] requested_profile="
                  << (has_requested_profile ? s.profile_name : "<none>") << "\n";
        std::cerr << "[profile-debug] loaded_config="
                  << (loaded_profile_file ? loaded_profile_path.string() : "<none>") << "\n";
        std::cerr << "[profile-debug] profile_count=" << profile_definitions.size() << "\n";
    }
    if (should_apply_profile) {
        auto profile_it = s.profiles.find(s.profile_name);
        if (profile_it == s.profiles.end()) {
            std::string available;
            for (size_t idx = 0; idx < profile_definitions.size(); ++idx) {
                if (idx > 0) {
                    available += ", ";
                }
                available += profile_definitions[idx].name;
            }
            std::cerr << tr("Invalid profile '") << s.profile_name << tr("'. Available profiles: ")
                      << available << "\n";
            return false;
        }

        const ProfileDefinition& selected_profile = profile_it->second;
        apply_profile(selected_profile, args, s,
                      has_source_resolution, source_w, source_h,
                      has_target_resolution, target_w, target_h,
                      resolution_reference);
        if (profile_debug) {
            std::cerr << "[profile-debug] applied_profile=" << selected_profile.name << "\n";
            std::cerr << "[profile-debug] mode=" << mode_name(s.mode)
                      << " optimize="
                      << (s.optimize_target == OptimizeTarget::GPU ? "gpu" : "space")
                      << " padding=" << s.padding
                      << " trim_transparent=" << (s.trim_transparent ? "true" : "false")
                      << " rotate=" << (s.allow_rotate ? "true" : "false")
                      << " scale=" << s.scale << "\n";
        }
    } else if (profile_debug) {
        std::cerr << "[profile-debug] applied_profile=<none>\n";
        std::cerr << "[profile-debug] using_cli_defaults=true\n";
    }

    if (has_source_resolution != has_target_resolution) {
        std::cerr << tr("Error: --source-resolution and --target-resolution must be provided together\n");
        return false;
    }
    if (has_source_resolution) {
        const double scale_x = static_cast<double>(target_w) / static_cast<double>(source_w);
        const double scale_y = static_cast<double>(target_h) / static_cast<double>(source_h);
        const double resolution_scale =
            (resolution_reference == ResolutionReference::Largest)
                ? std::max(scale_x, scale_y)
                : std::min(scale_x, scale_y);
        s.scale *= resolution_scale;
    }

    // Extruded edges live in the padding.
    if (s.padding < s.extrude * 2) {
        s.padding = s.extrude * 2;
    }
    return true;
}

bool open_input(const LayoutArgs& args, const fs::path& cwd, InputContext& input) {
    if (args.stdin_list) {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_TEXT);
#endif
        open_stdin_list_input(input, cwd);
        return true;
    }
    if (args.folder == "-") {
        if (!open_stdin_archive_input(input)) {
            std::cerr << tr("Error: Failed to load content from stdin\n");
            return false;
        }
        return true;
    }
    return open_path_input(args.folder, input);
}

// Scales the packed (trimmed) size of each sprite. Trims stay in original-image pixels, and
// exporters scale them separately (sprat::core::metadata_trim). Rounding the content and the
// margins separately lets their sum drift from the scaled original size, so frames of one
// animation would get different canvas sizes. The content size is therefore whatever is left
// of the rounded canvas once the rounded margins are taken off.
bool scale_sprites(std::vector<Sprite>& sprites, double scale) {
    if (scale == 1.0) {
        return true;
    }
    for (auto& s : sprites) {
        int scaled_w = 0;
        int scaled_h = 0;
        if (!scale_dimension(s.w, scale, scaled_w) || !scale_dimension(s.h, scale, scaled_h)) {
            std::cerr << tr("Error: scaled sprite dimensions are invalid\n");
            return false;
        }
        sprat::core::Sprite trimmed;  // the layout's view of this sprite, for the shared trim rule
        trimmed.path = s.path;
        trimmed.src_x = s.trim_left;
        trimmed.src_y = s.trim_top;
        trimmed.trim_right = s.trim_right;
        trimmed.trim_bottom = s.trim_bottom;
        const sprat::core::MetadataTrim margins = sprat::core::metadata_trim(trimmed, 1.0);
        const sprat::core::MetadataTrim scaled_margins = sprat::core::metadata_trim(trimmed, scale);
        int canvas_w = 0;
        int canvas_h = 0;
        if (scale_dimension(s.w + margins.left + margins.right, scale, canvas_w) &&
            scale_dimension(s.h + margins.top + margins.bottom, scale, canvas_h)) {
            scaled_w = std::max(1, canvas_w - scaled_margins.left - scaled_margins.right);
            scaled_h = std::max(1, canvas_h - scaled_margins.top - scaled_margins.bottom);
        }
        s.w = scaled_w;
        s.h = scaled_h;
    }
    return true;
}

// Fills the sprite sizes and atlas bounds of the search request.
bool measure_sprites(const std::vector<Sprite>& sprites, const LayoutSettings& settings,
                     SearchRequest& request) {
    const int padding = settings.padding;
    int max_width = 0;
    int max_height = 0;
    int sum_width = 0;
    int sum_height = 0;
    size_t total_area = 0;
    for (const auto& s : sprites) {
        int padded_w = 0;
        int padded_h = 0;
        if (!checked_add_int(s.w, padding, padded_w) || !checked_add_int(s.h, padding, padded_h)) {
            std::cerr << tr("Error: dimensions are too large\n");
            return false;
        }

        size_t sprite_area = 0;
        if (!checked_mul_size_t(static_cast<size_t>(padded_w), static_cast<size_t>(padded_h), sprite_area) ||
            sprite_area > std::numeric_limits<size_t>::max() - total_area) {
            std::cerr << tr("Error: total area is too large\n");
            return false;
        }
        total_area += sprite_area;

        max_width = std::max(max_width, padded_w);
        max_height = std::max(max_height, padded_h);
        if (!checked_add_int(sum_width, padded_w, sum_width) ||
            !checked_add_int(sum_height, padded_h, sum_height)) {
            std::cerr << tr("Error: dimensions are too large\n");
            return false;
        }
    }

    int width_upper_bound = sum_width;
    int height_upper_bound = sum_height;
    if (settings.max_width_limit > 0) {
        width_upper_bound = std::min(width_upper_bound, settings.max_width_limit);
    }
    if (settings.max_height_limit > 0) {
        height_upper_bound = std::min(height_upper_bound, settings.max_height_limit);
    }
    // POT atlases can only be as large as the largest power of two within each limit.
    const auto effective_limit = [&](int limit) {
        return settings.mode == Mode::POT ? previous_power_of_two(limit) : limit;
    };
    const int width_limit = settings.max_width_limit > 0 ? effective_limit(settings.max_width_limit) : 0;
    const int height_limit = settings.max_height_limit > 0 ? effective_limit(settings.max_height_limit) : 0;
    if ((width_limit > 0 && max_width > width_limit) || (height_limit > 0 && max_height > height_limit) ||
        max_width > width_upper_bound || max_height > height_upper_bound) {
        std::cerr << tr("Error: sprites (up to ") << max_width << tr(" wide and ") << max_height
                  << tr(" high, with padding) exceed the atlas limit");
        if (width_limit > 0 || height_limit > 0) {
            std::cerr << " (" << (width_limit > 0 ? std::to_string(width_limit) : std::string("-")) << "x"
                      << (height_limit > 0 ? std::to_string(height_limit) : std::string("-"));
            if (settings.mode == Mode::POT) {
                std::cerr << tr(", the largest power of two within --max-width/--max-height");
            }
            std::cerr << ")";
        }
        std::cerr << "\n";
        return false;
    }

    request.max_sprite_width = max_width;
    request.max_sprite_height = max_height;
    request.sum_width = sum_width;
    request.sum_height = sum_height;
    request.total_area = total_area;
    request.max_width_limit = settings.max_width_limit;
    request.max_height_limit = settings.max_height_limit;
    request.width_upper_bound = width_upper_bound;
    request.height_upper_bound = height_upper_bound;
    return true;
}

// The output options of a run, shared with prewarmed layouts so they match a real run.
LayoutOutputOptions make_output_options(const LayoutArgs& args, const LayoutSettings& settings,
                                        const fs::path& root) {
    LayoutOutputOptions options;
    options.scale = settings.scale;
    options.extrude = settings.extrude;
    options.multipack = settings.multipack;
    options.rotate_ccw = settings.rotate_ccw;
    options.debug = args.debug;
    options.root = root;
    return options;
}

// The layout cache files of this run.
struct CacheFiles {
    fs::path base;
    std::string layout_signature;
    fs::path layout;
    std::string seed_signature;
    fs::path seed;
    std::string incremental_signature;  // empty unless --incremental applies
    fs::path incremental;
};

// A compact search finds the best layouts for both optimize targets, so the
// compact profiles that differ from this run only in the optimize target (and
// name) can be cached too.
void prewarm_compact_profiles(const LayoutArgs& args, const LayoutSettings& settings,
                              const LayoutCacheKey& key, const CacheFiles& cache,
                              const std::vector<ImageSource>& sources,
                              const std::vector<sprat::core::Alias>& aliases,
                              const CompactSearchResult& result, const fs::path& root) {
    for (const char* profile_name : k_compact_prewarm_profiles) {
        auto prewarm_it = settings.profiles.find(profile_name);
        if (prewarm_it == settings.profiles.end()) {
            continue;
        }
        const ProfileDefinition& compact_profile = prewarm_it->second;
        const Mode prewarm_mode =
            args.has_mode_override ? args.mode_override : compact_profile.mode;
        const OptimizeTarget prewarm_optimize_target =
            args.has_optimize_override ? args.optimize_override : compact_profile.optimize_target;
        if (prewarm_mode != Mode::COMPACT) {
            continue;
        }
        const int prewarm_max_width =
            args.has_max_width_limit ? settings.max_width_limit : compact_profile.max_width.value_or(0);
        const int prewarm_max_height =
            args.has_max_height_limit ? settings.max_height_limit : compact_profile.max_height.value_or(0);
        const int prewarm_padding =
            args.has_padding_override ? settings.padding : compact_profile.padding.value_or(0);
        const double prewarm_scale =
            args.has_scale_override ? settings.scale : compact_profile.scale.value_or(1.0);
        const bool prewarm_trim_transparent =
            args.has_trim_override ? settings.trim_transparent : compact_profile.trim_transparent.value_or(false);
        const int prewarm_extrude =
            args.has_extrude_override ? settings.extrude : compact_profile.extrude.value_or(k_default_extrude);
        const bool prewarm_rotate =
            args.has_rotate_override ? settings.allow_rotate : compact_profile.rotate.value_or(false);
        const bool prewarm_rotate_ccw =
            args.has_rotate_direction_override ? settings.rotate_ccw : compact_profile.rotate_ccw.value_or(false);
        const bool prewarm_uses_resolution =
            (!args.has_source_resolution && compact_profile.source_resolution)
            || (!args.has_target_resolution && compact_profile.target_resolution);
        // The candidates were packed with this run's inputs; they are only valid
        // for a profile that differs in nothing but the optimize target.
        if (prewarm_max_width != settings.max_width_limit || prewarm_max_height != settings.max_height_limit
            || prewarm_padding != settings.padding || prewarm_scale != settings.scale
            || prewarm_trim_transparent != settings.trim_transparent || prewarm_extrude != settings.extrude
            || prewarm_rotate != settings.allow_rotate || prewarm_rotate_ccw != settings.rotate_ccw
            || prewarm_uses_resolution) {
            continue;
        }
        LayoutCacheKey prewarm_key = key;
        prewarm_key.profile_name = compact_profile.name;
        prewarm_key.mode = prewarm_mode;
        prewarm_key.optimize_target = prewarm_optimize_target;
        prewarm_key.multipack = false;
        const std::string prewarm_signature = build_layout_signature(prewarm_key, sources);
        if (prewarm_signature == cache.layout_signature) {
            continue;
        }

        const LayoutCandidate& prewarm_candidate =
            prewarm_optimize_target == OptimizeTarget::GPU ? result.best_gpu : result.best_space;
        // Same final bounds as a run of that profile (see tighten_atlas_bounds).
        int prewarm_width = prewarm_candidate.w;
        int prewarm_height = prewarm_candidate.h;
        if (settings.padding > 0 &&
            !compute_tight_atlas_bounds(prewarm_candidate.sprites, prewarm_width, prewarm_height)) {
            continue;
        }
        const std::vector<Atlas> prewarm_atlases = {{prewarm_width, prewarm_height}};
        LayoutOutputOptions prewarm_options = make_output_options(args, settings, root);
        prewarm_options.debug = false;  // cached layouts never carry the debug header
        // Deduplication ran with the same settings and trimming, so the aliases are the same.
        const std::string prewarm_output = build_layout_output_text(
            prewarm_atlases, prewarm_candidate.sprites, aliases, prewarm_options);
        save_output_cache(build_output_cache_path(cache.base, prewarm_signature), prewarm_signature, prewarm_output);
    }
}

// Packs all sprites into one atlas with the selected mode.
bool pack_single_atlas(const LayoutArgs& args, const LayoutSettings& settings,
                       const SearchRequest& request, const LayoutCacheKey& key,
                       const CacheFiles& cache, const std::vector<ImageSource>& sources,
                       const std::vector<sprat::core::Alias>& aliases,
                       const fs::path& root, std::vector<Sprite>& sprites,
                       int& atlas_width, int& atlas_height) {
    switch (settings.mode) {
        case Mode::POT:
            return search_pot_layout(request, sprites, atlas_width, atlas_height);
        case Mode::GRID:
            return search_grid_layout(request, sprites, atlas_width, atlas_height);
        case Mode::FAST:
            return search_fast_layout(request, sprites, atlas_width, atlas_height);
        case Mode::COMPACT:
            break;
    }

    CompactSearchResult result;
    if (!search_compact_layout(request, sprites, result)) {
        return false;
    }
    const LayoutCandidate* selected = select_compact_candidate(result, settings.optimize_target);
    if (selected == nullptr || !selected->valid) {
        std::cerr << tr("Error: failed to compute compact layout\n");
        return false;
    }
    sprites = selected->sprites;
    atlas_width = selected->w;
    atlas_height = selected->h;
    if (result.best_gpu.valid && result.best_space.valid) {
        prewarm_compact_profiles(args, settings, key, cache, sources, aliases, result, root);
    }
    return true;
}

// Reused positions (layout seed or --incremental) only give the extent of the sprites. A pot
// atlas still needs power-of-two sides within the limits, as search_pot_layout picks them;
// returns false when the reused positions can't give one, so the layout is searched again.
bool fit_reused_atlas(const LayoutSettings& settings, int& width, int& height) {
    if (settings.mode != Mode::POT) {
        return true;
    }
    const int pot_width = next_power_of_two(width);
    const int pot_height = next_power_of_two(height);
    if (pot_width <= 0 || pot_height <= 0 ||
        (settings.max_width_limit > 0 && pot_width > settings.max_width_limit) ||
        (settings.max_height_limit > 0 && pot_height > settings.max_height_limit)) {
        return false;
    }
    width = pot_width;
    height = pot_height;
    return true;
}

// Packing reserves padding after the last sprite of each row and column; drop it.
bool tighten_atlas_bounds(const std::vector<Sprite>& sprites, bool multipack,
                          std::vector<Atlas>& atlases, int& atlas_width, int& atlas_height) {
    if (multipack) {
        for (size_t ai = 0; ai < atlases.size(); ++ai) {
            int tight_w = 0;
            int tight_h = 0;
            for (const auto& s : sprites) {
                if (s.atlas_index != static_cast<int>(ai)) {
                    continue;
                }
                int x1 = 0;
                int y1 = 0;
                if (!checked_add_int(s.x, s.w, x1) || !checked_add_int(s.y, s.h, y1)) {
                    std::cerr << tr("Error: failed to compute final atlas bounds\n");
                    return false;
                }
                tight_w = std::max(x1, tight_w);
                tight_h = std::max(y1, tight_h);
            }
            if (tight_w > 0 && tight_h > 0) {
                atlases[ai].width = tight_w;
                atlases[ai].height = tight_h;
            }
        }
        return true;
    }
    if (!compute_tight_atlas_bounds(sprites, atlas_width, atlas_height)) {
        std::cerr << tr("Error: failed to compute final atlas bounds\n");
        return false;
    }
    if (!atlases.empty()) {
        atlases[0].width = atlas_width;
        atlases[0].height = atlas_height;
    }
    return true;
}

} // namespace

int run_spratlayout(int argc, char** argv) {
#ifdef _WIN32
    // Set stdout to binary mode to avoid \r\n translation in layout output.
    // Suppress failure: when running as a subprocess of a GUI application the
    // pipe handle may not support _setmode; this is non-fatal.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    LayoutArgs args;
    const int early_exit = try_parse_args(argc, argv, args);
    if (early_exit >= 0) {
        return early_exit;
    }
#ifdef _WIN32
    // Set stdin to binary mode only when --stdin-list is active so path data
    // read from stdin is not corrupted by \r\n translation.
    if (args.stdin_list) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
#endif

    const fs::path exec_dir = sprat::core::get_executable_dir(argv[0]);
    if (args.show_profiles_config) {
        for (const fs::path& candidate : build_default_profiles_config_candidates(exec_dir)) {
            std::error_code ec;
            if (fs::exists(candidate, ec) && !ec) {
                std::cout << candidate.string() << "\n";
                return 0;
            }
        }
        std::cout << k_global_profiles_config_path << "\n";
        return 0;
    }

    if (args.folder.empty() && !args.stdin_list) {
        print_usage();
        return 1;
    }

    const fs::path cwd = fs::current_path();
    LayoutSettings settings;
    if (!resolve_settings(args, cwd, exec_dir, settings)) {
        return 1;
    }

    InputContext input_context;
    if (!open_input(args, cwd, input_context)) {
        return 1;
    }
    const fs::path root = layout_root(input_context);

    CacheFiles cache;
    // Archives are extracted to a new folder on every run, so their layouts are not cached.
    if (input_context.type != InputType::Archive) {
        cache.base = build_cache_path(input_context.working_folder);
    }
    const long long now_unix = now_unix_seconds();
    prune_all_caches();
    prune_cache_family(cache.base);

    std::vector<ImageSource> sources;
    if (!collect_image_sources(input_context, sources)) {
        return 1;
    }

    const bool enforce_name_order   = (args.has_frame_sort_override && args.frame_sort == FrameSort::Name);
    const bool enforce_stable_order = (args.has_frame_sort_override && args.frame_sort == FrameSort::Stable);
    if (enforce_name_order || enforce_stable_order) {
        std::ranges::sort(sources, [](const ImageSource& lhs, const ImageSource& rhs) {
            int cmp = sprat::core::compare_natural(lhs.path, rhs.path);
            if (cmp != 0) {
                return cmp < 0;
            }
            if (lhs.meta.file_size != rhs.meta.file_size) {
                return lhs.meta.file_size < rhs.meta.file_size;
            }
            return lhs.meta.mtime_ticks < rhs.meta.mtime_ticks;
        });
    }

    if (sources.empty()) {
        std::cerr << tr("Error: no valid images found\n");
        return 1;
    }

    LayoutCacheKey key;
    key.profile_name = settings.profile_name;
    key.mode = settings.mode;
    key.optimize_target = settings.optimize_target;
    key.max_width_limit = settings.max_width_limit;
    key.max_height_limit = settings.max_height_limit;
    key.padding = settings.padding;
    key.extrude = settings.extrude;
    key.scale = settings.scale;
    key.trim_transparent = settings.trim_transparent;
    key.allow_rotate = settings.allow_rotate;
    key.preserve_source_order = !enforce_name_order && !enforce_stable_order;
    key.multipack = settings.multipack;
    key.rotate_ccw = settings.rotate_ccw;
    key.order_key = build_order_key(args.has_frame_sort_override, args.frame_sort, args.stable_metric);
    key.deduplicate_mode = args.deduplicateMode;
    key.dedup_threshold = args.dedup_threshold;
    key.dedup_tolerance = args.dedup_tolerance;
    cache.layout_signature = build_layout_signature(key, sources);
    cache.seed_signature = build_layout_seed_signature(key, sources);
    // --debug adds a comment header, so it neither reads nor writes the layout cache.
    if (!args.debug) {
        cache.layout = build_output_cache_path(cache.base, cache.layout_signature);
    }
    cache.seed = build_seed_cache_path(cache.base, cache.seed_signature);
    // Grid cells come from the sprite order and size alone, so grid layouts are never pieced
    // together from earlier positions (that would also skip the same-size check).
    const bool reuse_positions = settings.mode != Mode::GRID;
    if (settings.incremental && !reuse_positions) {
        std::cerr << tr("Warning: --incremental is ignored in grid mode\n");
    }
    const bool use_incremental = settings.incremental && !settings.multipack && reuse_positions;
    if (use_incremental) {
        cache.incremental_signature = build_incremental_seed_signature(key, input_context.working_folder);
        cache.incremental = build_incremental_seed_cache_path(cache.base, cache.incremental_signature);
    }

    if (is_cache_file_fresh(cache.layout)) {
        std::string cached_output;
        if (load_output_cache(cache.layout, cache.layout_signature, cached_output)) {
            std::cout << cached_output;
            return 0;
        }
    }

    ImageCache cache_entries;
    load_image_cache(cache.base, cache_entries);
    prune_stale_cache_entries(cache_entries, now_unix);
    SpriteLoadOptions load_options;
    load_options.trim_transparent = settings.trim_transparent;
    load_options.deduplicate_mode = args.deduplicateMode;
    load_options.thread_limit = settings.thread_limit;
    std::vector<Sprite> sprites = load_sprites(sources, load_options, cache_entries, now_unix);
    save_image_cache(cache.base, cache_entries);

    std::vector<sprat::core::Alias> layout_aliases;
    deduplicate_sprites(sprites, args.deduplicateMode, settings.trim_transparent,
                        args.dedup_threshold, args.dedup_tolerance, cache_entries, layout_aliases);

    if (sprites.empty()) {
        std::cerr << tr("Error: no valid images found\n");
        return 1;
    }
    if (!scale_sprites(sprites, settings.scale)) {
        return 1;
    }

    SearchRequest request;
    if (!measure_sprites(sprites, settings, request)) {
        return 1;
    }
    request.padding = settings.padding;
    request.allow_rotate = settings.allow_rotate;
    request.optimize_target = settings.optimize_target;
    request.enforce_sort_order = enforce_name_order || enforce_stable_order;
    request.enforce_stable_order = enforce_stable_order;
    request.stable_metric = args.stable_metric;
    request.thread_limit = settings.thread_limit;

    if (enforce_stable_order) {
        sort_sprites_stable(sprites, args.stable_metric);
    }

    // Reuse a previous layout when possible: first the incremental seed (keeps
    // unchanged sprites in place), then the exact seed of the same sprites.
    int atlas_width = 0;
    int atlas_height = 0;
    bool reused_layout_seed = false;
    LayoutSeedCache seed_cache;
    bool have_layout_seed = false;
    std::vector<Sprite> seeded_sprites;
    if (use_incremental) {
        LayoutSeedCache incremental_seed;
        if (is_cache_file_fresh(cache.incremental) &&
            load_layout_seed_cache(cache.incremental, cache.incremental_signature, incremental_seed) &&
            incremental_seed.padding == settings.padding &&
            pack_incremental_maxrects(incremental_seed, settings.padding,
                                      request.width_upper_bound, request.height_upper_bound,
                                      sprites, settings.allow_rotate,
                                      seeded_sprites, atlas_width, atlas_height) &&
            fit_reused_atlas(settings, atlas_width, atlas_height)) {
            sprites = std::move(seeded_sprites);
            reused_layout_seed = true;
        }
    }
    if (!reused_layout_seed && reuse_positions &&
        is_cache_file_fresh(cache.seed) &&
        load_layout_seed_cache(cache.seed, cache.seed_signature, seed_cache) &&
        seed_cache.padding == settings.padding) {
        have_layout_seed = true;
        if (try_apply_layout_seed(seed_cache, settings.padding,
                                  request.width_upper_bound, request.height_upper_bound,
                                  sprites, seeded_sprites, atlas_width, atlas_height) &&
            fit_reused_atlas(settings, atlas_width, atlas_height)) {
            sprites = std::move(seeded_sprites);
            reused_layout_seed = true;
        }
    }
    request.seed_hint = have_layout_seed ? &seed_cache : nullptr;

    std::vector<Atlas> atlases;
    if (reused_layout_seed) {
        atlases.push_back({atlas_width, atlas_height});
        for (auto& s : sprites) { s.atlas_index = 0; }
    } else if (settings.multipack) {
        // The upper bounds are min(limit, sum of sprite sizes). For pot atlases only the limit is
        // a hard cap (rounded down to a power of two); the sum is just a search bound, so it is
        // rounded up, or a small sheet could not fit the power of two its sprites need.
        int multipack_w = request.width_upper_bound;
        int multipack_h = request.height_upper_bound;
        if (settings.mode == Mode::POT) {
            const auto pot_bound = [](int sum, int limit) {
                const int up = next_power_of_two(sum);
                const int cap = limit > 0 ? previous_power_of_two(limit) : up;
                return up > 0 ? std::min(up, cap) : cap;
            };
            multipack_w = pot_bound(request.sum_width, settings.max_width_limit);
            multipack_h = pot_bound(request.sum_height, settings.max_height_limit);
        }
        if (!pack_atlases(sprites, multipack_w, multipack_h, settings.padding,
                          settings.mode, settings.optimize_target, settings.allow_rotate,
                          request.enforce_sort_order, atlases)) {
            std::cerr << tr("Error: failed to compute multipack layout\n");
            return 1;
        }
    } else {
        if (!pack_single_atlas(args, settings, request, key, cache, sources, layout_aliases, root,
                               sprites, atlas_width, atlas_height)) {
            return 1;
        }
        atlases.push_back({atlas_width, atlas_height});
        for (auto& s : sprites) { s.atlas_index = 0; }
    }

    // Grid cells and power-of-two sizes are the point of those modes; keep them.
    if (settings.padding > 0 && settings.mode != Mode::GRID && settings.mode != Mode::POT &&
        !tighten_atlas_bounds(sprites, settings.multipack, atlases, atlas_width, atlas_height)) {
        return 1;
    }

    if (!settings.multipack) {
        save_layout_seed_cache(cache.seed, make_layout_seed(
            cache.seed_signature, settings.padding, atlas_width, atlas_height, sprites));
        if (use_incremental) {
            save_layout_seed_cache(cache.incremental, make_layout_seed(
                cache.incremental_signature, settings.padding, atlas_width, atlas_height, sprites));
        }
    }

    const std::string output_text = build_layout_output_text(
        atlases, sprites, layout_aliases, make_output_options(args, settings, root));

#ifdef _WIN32
    // Suppress failure: non-fatal when running embedded or as a GUI subprocess.
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::cout << output_text;
    if (!settings.multipack) {
        save_output_cache(cache.layout, cache.layout_signature, output_text);
    }
    prune_cache_family(cache.base);
    return 0;
}
