// Validation for names that become output file paths.
#pragma once

#include <string>
#include <string_view>

namespace sprat::core {

// Windows opens a device instead of a file for these names, with any extension
// ("con.anim", "nul.json"), so writing them would silently go nowhere.
bool is_windows_reserved_name(std::string_view name);

// A single file name directly inside an output directory: no folders, no parent
// references, no characters Windows rejects, no device names, no trailing dot or space.
bool is_safe_output_filename(std::string_view name);

// A relative path with '/' separators, such as "hero/idle.png". Rejects absolute
// paths, drive letters, alternate data streams, backslashes, "."/".." segments,
// empty segments, device names and characters that are invalid on any platform.
bool is_safe_relative_output_path(std::string_view path, std::string& error);

} // namespace sprat::core
