// Input handling: folders, list files, archives and stdin, and the image sources they name.
#pragma once

#include "commands/spratlayout/common.h"

#include <cstdint>
#include <vector>

namespace sprat::layout {

enum class InputType : std::uint8_t {
    Directory,
    ListFile,
    TarFile,
    StdinTar,
    StdinList
};

struct InputContext {
    InputType type = InputType::Directory;
    fs::path working_folder;
    // Folders archives were extracted to; removed when the context is destroyed.
    std::vector<fs::path> temp_dirs_to_cleanup;

    InputContext() = default;
    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;
    ~InputContext();
};

// Opens a folder, list file or archive (archives are extracted to a temporary folder).
bool open_path_input(const fs::path& input_path, InputContext& out_context);
// Extracts an archive read from stdin.
bool open_stdin_archive_input(InputContext& out_context);
// Image paths will be read from stdin, relative to cwd.
void open_stdin_list_input(InputContext& out_context, const fs::path& cwd);

// Archives are extracted to a new folder on every run, so their layouts are not cached.
bool is_extracted_archive(const InputContext& input);

// The folder that sprite paths in the layout are written relative to.
fs::path layout_root(const InputContext& input);

// Collects the images named by the input, honoring .spratlayoutignore and
// "exclude"/"root" directives. Returns false after printing an error.
bool collect_image_sources(const InputContext& input, std::vector<ImageSource>& out);

} // namespace sprat::layout
