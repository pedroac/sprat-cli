// Option values and the spratprofiles.cfg format.
#pragma once

#include "commands/spratlayout/common.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sprat::layout {

struct ProfileDefinition {
    std::string name;
    std::string label;
    Mode mode = Mode::COMPACT;
    OptimizeTarget optimize_target = OptimizeTarget::GPU;
    std::optional<int> max_width;
    std::optional<int> max_height;
    std::optional<int> padding;
    std::optional<int> extrude;
    std::optional<double> scale;
    std::optional<bool> trim_transparent;
    std::optional<bool> rotate;
    std::optional<bool> rotate_ccw;
    std::optional<bool> multipack;
    std::optional<unsigned int> threads;
    std::optional<bool> incremental;
    std::optional<std::pair<int, int>> source_resolution;
    // (-1, -1) means "same as source_resolution".
    std::optional<std::pair<int, int>> target_resolution;
    std::optional<ResolutionReference> resolution_reference;
};

struct PresetDefinition {
    Mode mode;
    OptimizeTarget optimize_target;
};

extern const char* const k_global_profiles_config_path;

bool parse_mode_from_string(const std::string& value, Mode& out, std::string& error);
bool parse_optimize_target_from_string(const std::string& value, OptimizeTarget& out, std::string& error);
bool parse_preset_from_string(const std::string& value, PresetDefinition& out);
bool parse_resolution_reference_from_string(const std::string& value,
                                            ResolutionReference& out,
                                            std::string& error);
// "cw" (default) or "ccw": direction of the 90-degree rotation of rotated sprites.
bool parse_rotate_direction(const std::string& value, bool& rotate_ccw);
bool parse_scale_factor(const std::string& value, double& out);
bool parse_resolution(const std::string& value, int& out_width, int& out_height);
bool parse_frame_sort_from_string(const std::string& value, FrameSort& out, StableMetric& out_metric);

bool load_profiles_config_from_file(const fs::path& path,
                                    std::vector<ProfileDefinition>& out,
                                    std::string& error);
// Lookup order: beside the executable, the user config directory, the global config.
std::vector<fs::path> build_default_profiles_config_candidates(const fs::path& exec_dir);

} // namespace sprat::layout
