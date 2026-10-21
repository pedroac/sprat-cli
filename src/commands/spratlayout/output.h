// The layout text format written to stdout (see core/layout_parser.h for the reader).
#pragma once

#include "commands/spratlayout/common.h"
#include "core/layout_parser.h"

#include <string>
#include <vector>

namespace sprat::layout {

struct LayoutOutputOptions {
    double scale = 1.0;
    int extrude = 0;
    bool multipack = false;
    bool rotate_ccw = false;
    bool debug = false;  // adds a comment header
    fs::path root;       // sprite paths are written relative to it
};

std::string build_layout_output_text(const std::vector<Atlas>& atlases,
                                     const std::vector<Sprite>& sprites,
                                     const std::vector<sprat::core::Alias>& aliases,
                                     const LayoutOutputOptions& options);

} // namespace sprat::layout
