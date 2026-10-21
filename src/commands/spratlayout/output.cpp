#include "commands/spratlayout/output.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "core/cli_parse.h"

namespace sprat::layout {

using sprat::core::to_quoted;

namespace {

constexpr int k_output_precision = 8;

// Layout paths are relative to the root and always use forward slashes.
std::string relative_layout_path(const std::string& path, const fs::path& root) {
    std::string relative = fs::relative(fs::path(path), root).string();
    std::ranges::replace(relative, '\\', '/');
    return relative;
}

void write_trim(std::ostream& output, int left, int top, int right, int bottom) {
    output << " " << left << "," << top << " " << right << "," << bottom;
}

} // namespace

std::string build_layout_output_text(const std::vector<Atlas>& atlases,
                                     const std::vector<Sprite>& sprites,
                                     const std::vector<sprat::core::Alias>& aliases,
                                     const LayoutOutputOptions& options) {
    std::ostringstream output;
    if (options.debug) {
        output << "# Sprat Layout Debug Info\n";
        output << "# Scale: " << options.scale << "\n";
        output << "# Atlases: " << atlases.size() << "\n";
        output << "# Total Sprites: " << sprites.size() << "\n";
        output << "# Aliases: " << aliases.size() << "\n";
        output << "# Multipack: " << (options.multipack ? "true" : "false") << "\n";
    }
    output << "root " << to_quoted(options.root.string()) << "\n";
    output << "scale " << std::setprecision(k_output_precision) << options.scale << "\n";
    if (options.extrude > 0) {
        output << "extrude " << options.extrude << "\n";
    }
    if (options.multipack) {
        output << "multipack true\n";
    }
    if (options.rotate_ccw) {
        output << "rotation ccw\n";
    }
    // Pre-group sprite indices by atlas_index to avoid O(sprites * atlases) scan.
    std::vector<std::vector<size_t>> sprites_by_atlas(atlases.size());
    for (size_t si = 0; si < sprites.size(); ++si) {
        int ai = sprites[si].atlas_index;
        if (ai >= 0 && static_cast<size_t>(ai) < atlases.size()) {
            sprites_by_atlas[static_cast<size_t>(ai)].push_back(si);
        }
    }
    for (size_t i = 0; i < atlases.size(); ++i) {
        output << "atlas " << atlases[i].width << "," << atlases[i].height << "\n";
        for (size_t si : sprites_by_atlas[i]) {
            const auto& s = sprites[si];
            output << "sprite " << to_quoted(relative_layout_path(s.path, options.root)) << " "
                   << s.x << "," << s.y << " "
                   << s.w << "," << s.h;
            if (s.trim_left || s.trim_top || s.trim_right || s.trim_bottom) {
                write_trim(output, s.trim_left, s.trim_top, s.trim_right, s.trim_bottom);
            }
            if (s.rotated) {
                output << " rotated";
            }
            if (s.has_slice) {
                output << " slice=" << s.slice_left << "," << s.slice_top
                       << "," << s.slice_right << "," << s.slice_bottom;
            }
            output << "\n";
        }
    }
    for (const auto& alias : aliases) {
        output << "alias " << to_quoted(relative_layout_path(alias.path, options.root))
               << " " << to_quoted(relative_layout_path(alias.canonical_path, options.root));
        if (alias.has_trim) {
            write_trim(output, alias.trim_left, alias.trim_top, alias.trim_right, alias.trim_bottom);
        }
        output << "\n";
    }
    return output.str();
}

} // namespace sprat::layout
