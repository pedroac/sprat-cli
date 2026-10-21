#include "commands/spratlayout/input.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>

#include "core/cli_parse.h"
#include "core/i18n.h"

namespace sprat::layout {

using sprat::core::to_quoted;
using sprat::core::tr;
using sprat::core::trim_copy;

namespace {

constexpr uintmax_t k_max_file_size = 1000000000; // 1GB
constexpr size_t k_tar_read_buffer_size = 10240;
constexpr size_t k_max_extension_len = 10;

std::string normalize_path_key(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (!ec) {
        return absolute.lexically_normal().string();
    }
    return path.lexically_normal().string();
}

bool read_image_meta(const fs::path& path, ImageMeta& out) {
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return false;
    }
    if (size > k_max_file_size) { // 1GB limit
        return false;
    }
    fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    out.file_size = size;
    out.mtime_ticks = mtime.time_since_epoch().count();
    return true;
}

bool is_supported_image_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty() || ext.size() > k_max_extension_len) {
        return false;
    }
    std::ranges::transform(ext, ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp"
           || ext == ".tga" || ext == ".gif" || ext == ".psd" || ext == ".pic"
           || ext == ".pnm" || ext == ".pgm" || ext == ".ppm" || ext == ".hdr"
           || ext == ".webp";
}

enum class ContentType : std::uint8_t {
    Directory,
    ListFile,
    TarFile,
    CompressedTarFile,
    ArchiveFile,
    Unknown
};

ContentType detect_content_type_from_path(const fs::path& path) {
    const bool is_dir = fs::exists(path) && fs::is_directory(path);
    const bool is_file = fs::exists(path) && fs::is_regular_file(path);
    
    if (is_dir) {
        return ContentType::Directory;
    }
    if (!is_file) {
        return ContentType::Unknown;
    }
    
    // For files, check extension first for quick detection
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    if (ext == ".tar") {
        return ContentType::TarFile;
    }
    
    if (ext == ".zip" || ext == ".7z" || ext == ".rar") {
        return ContentType::ArchiveFile;
    }
    
    std::string filename = path.filename().string();
    std::ranges::transform(filename, filename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    // Check for compressed tar extensions
    if (filename.find(".tar.gz") != std::string::npos
        || filename.find(".tar.bz2") != std::string::npos
        || filename.find(".tar.xz") != std::string::npos
        || filename.find(".tgz") != std::string::npos
        || filename.find(".tbz2") != std::string::npos
        || filename.find(".txz") != std::string::npos) {
        return ContentType::CompressedTarFile;
    }
    
    // For other files, assume list file
    return ContentType::ListFile;
}

bool is_tar_file(const fs::path& path) {
    ContentType type = detect_content_type_from_path(path);
    return type == ContentType::TarFile || type == ContentType::ArchiveFile;
}

bool is_compressed_tar_file(const fs::path& path) {
    return detect_content_type_from_path(path) == ContentType::CompressedTarFile;
}

// Returns an empty path if entry_name would escape output_dir (Zip Slip guard).
fs::path safe_extract_path(const fs::path& output_dir, const char* entry_name) {
    if (entry_name == nullptr) {
        return {};
    }
    fs::path entry(entry_name);
    if (entry.is_absolute()) {
        return {};
    }
    fs::path candidate = (output_dir / entry).lexically_normal();
    fs::path rel = candidate.lexically_relative(output_dir.lexically_normal());
    if (rel.empty() || rel.begin()->string() == "..") {
        return {};
    }
    return candidate;
}

// Extracts the regular-file entries of an opened archive reader below output_dir,
// then frees the reader.
bool extract_archive(struct archive* a, const fs::path& output_dir) {
    struct archive* ext = archive_write_disk_new();
    if (ext == nullptr) {
        std::cerr << tr("Error: Failed to create archive writer") << '\n';
        archive_read_free(a);
        return false;
    }

    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);

    bool had_error = false;
    struct archive_entry* entry = nullptr;

    while (true) {
        la_ssize_t r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r < ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to read archive header: ") << archive_error_string(a) << '\n';
            had_error = true;
            break;
        }

        // Skip directories
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }

        // Validate and resolve extraction path (guards against Zip Slip)
        fs::path output_path = safe_extract_path(output_dir, archive_entry_pathname(entry));
        if (output_path.empty()) {
            std::cerr << tr("Warning: Skipping archive entry with unsafe path: ")
                      << (archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "(null)") << '\n';
            continue;
        }

        // Create parent directory if needed
        std::error_code ec;
        fs::create_directories(output_path.parent_path(), ec);

        // Set the extraction path
        archive_entry_set_pathname(entry, output_path.string().c_str());

        // Extract the file
        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            std::cerr << tr("Error: Failed to write archive header: ") << archive_error_string(ext) << '\n';
            had_error = true;
        } else {
            const void* buff = nullptr;
            size_t size = 0;
            la_int64_t offset = 0;
            bool block_error = false;

            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                if (archive_write_data_block(ext, buff, size, offset) < ARCHIVE_OK) {
                    std::cerr << tr("Error: Failed to write archive data: ") << archive_error_string(ext) << '\n';
                    had_error = true;
                    block_error = true;
                    break;
                }
            }

            if (!block_error && archive_write_finish_entry(ext) < ARCHIVE_OK) {
                std::cerr << tr("Error: Failed to finish archive entry: ") << archive_error_string(ext) << '\n';
                had_error = true;
            }
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return !had_error;
}

// A reader that accepts every archive format and compression libarchive supports.
struct archive* new_archive_reader() {
    struct archive* a = archive_read_new();
    if (a == nullptr) {
        std::cerr << tr("Error: Failed to create archive reader") << '\n';
        return nullptr;
    }
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    return a;
}

bool extract_tar_file(const fs::path& tar_path, const fs::path& output_dir) {
    struct archive* a = new_archive_reader();
    if (a == nullptr) {
        return false;
    }
    if (archive_read_open_filename(a, tar_path.string().c_str(), k_tar_read_buffer_size) != ARCHIVE_OK) {
        std::cerr << tr("Error: Failed to open archive file: ") << archive_error_string(a) << '\n';
        archive_read_free(a);
        return false;
    }
    return extract_archive(a, output_dir);
}

bool extract_tar_from_stdin(const fs::path& output_dir) {
    struct archive* a = new_archive_reader();
    if (a == nullptr) {
        return false;
    }

    // Use a read callback so that input flows through std::cin.  In embedded
    // mode std::cin is redirected to a stringstream; reading from the OS-level
    // stdin fd would bypass that redirection and receive no data.
    auto tar_read_cb = [](struct archive* a, void* /*client_data*/,
                          const void** buffer) -> la_ssize_t {
        static thread_local std::vector<char> buf(k_tar_read_buffer_size);
        std::cin.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (std::cin.bad() || (std::cin.fail() && !std::cin.eof())) {
            archive_set_error(a, EIO, "std::cin read error");
            return -1;
        }
        auto n = std::cin.gcount();
        *buffer = buf.data();
        return static_cast<la_ssize_t>(n);
    };
    if (archive_read_open(a, nullptr, nullptr, tar_read_cb, nullptr) != ARCHIVE_OK) {
        std::cerr << tr("Error: Failed to open stdin for archive extraction: ") << archive_error_string(a) << '\n';
        archive_read_free(a);
        return false;
    }
    return extract_archive(a, output_dir);
}

// Creates a new, private folder in the temp directory. Every run gets its own folder,
// so files of an earlier or concurrent run can never mix into this one.
bool create_extraction_dir(const std::string& prefix, fs::path& out) {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) {
        return false;
    }
    std::random_device rng;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const uint64_t id = ((static_cast<uint64_t>(rng()) << 32) | rng()) ^ ticks;
        std::array<char, 17> hex{};
        std::snprintf(hex.data(), hex.size(), "%016llx", static_cast<unsigned long long>(id));
        fs::path dir = base / (prefix + hex.data());
        // create_directory returns false, without an error, if the folder already exists.
        if (fs::create_directory(dir, ec)) {
            fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
            out = std::move(dir);
            return true;
        }
        if (ec) {
            return false;
        }
    }
    return false;
}

// Paths excluded by .spratlayoutignore or "exclude" lines: absolute keys, plus the
// path as written when it was relative.
class ExclusionSet {
public:
    void add(const fs::path& path, const std::string* relative_key = nullptr) {
        paths_.insert(normalize_path_key(path));
        if (relative_key != nullptr && !relative_key->empty()) {
            paths_.insert(*relative_key);
        }
    }

    bool contains(const fs::path& path, const fs::path* root) const {
        if (paths_.contains(normalize_path_key(path))) {
            return true;
        }
        if (root != nullptr) {
            std::error_code ec;
            fs::path relative = fs::relative(path, *root, ec);
            if (!ec && paths_.contains(relative.lexically_normal().string())) {
                return true;
            }
        }
        return false;
    }

private:
    std::unordered_set<std::string> paths_;
};

// True if `line` is `keyword` alone or followed by whitespace; pos is then set to
// the first character of the argument.
bool match_directive(const std::string& line, std::string_view keyword, size_t& pos) {
    if (line.size() < keyword.size() || line.compare(0, keyword.size(), keyword) != 0) {
        return false;
    }
    if (line.size() != keyword.size() && !std::isspace(static_cast<unsigned char>(line[keyword.size()]))) {
        return false;
    }
    pos = keyword.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }
    return true;
}

class SourceCollector {
public:
    SourceCollector(const InputContext& input, std::vector<ImageSource>& out)
        : input_(input), sources_(out) {}

    bool collect() {
        if (input_.type == InputType::Directory || input_.type == InputType::TarFile ||
            input_.type == InputType::StdinTar) {
            // Archives are already extracted to working_folder; only real folders have an ignore file.
            if (input_.type == InputType::Directory) {
                load_ignore_file(input_.working_folder / ".spratlayoutignore", input_.working_folder);
            }
            for (const auto& entry : fs::recursive_directory_iterator(input_.working_folder)) {
                if (entry.is_regular_file()) {
                    add_source(entry.path(), false);
                }
            }
            return true;
        }

        if (input_.type == InputType::StdinList) {
            return parse_list_stream(std::cin, input_.working_folder);
        }
        std::ifstream list_file(input_.working_folder);
        if (!list_file) {
            std::cerr << tr("Failed to open list file: ") << input_.working_folder << "\n";
            return false;
        }
        return parse_list_stream(list_file, input_.working_folder.parent_path());
    }

private:
    // Lines are paths, optionally prefixed by "exclude"; invalid lines are ignored.
    void load_ignore_file(const fs::path& file_path, const fs::path& base_root) {
        std::ifstream in(file_path);
        if (!in) {
            return;
        }
        std::string line;
        while (std::getline(in, line)) {
            std::string trimmed = trim_copy(line);
            if (trimmed.empty() || trimmed.front() == '#') {
                continue;
            }

            std::string path_text;
            size_t pos = 0;
            if (match_directive(trimmed, "exclude", pos)) {
                if (pos < trimmed.size() && trimmed[pos] == '"') {
                    std::string error;
                    if (!sprat::core::parse_quoted(trimmed, pos, path_text, error)) {
                        continue;
                    }
                } else if (pos < trimmed.size()) {
                    path_text = trimmed.substr(pos);
                }
            } else {
                path_text = trimmed;
            }
            if (path_text.empty()) {
                continue;
            }

            fs::path excluded_path(path_text);
            std::optional<std::string> relative_key;
            if (excluded_path.is_relative()) {
                relative_key = excluded_path.lexically_normal().string();
                excluded_path = base_root / excluded_path;
            }
            excluded_.add(excluded_path, relative_key ? &*relative_key : nullptr);
        }
    }

    // List format (list files and --stdin-list): one image path per line, plus
    // "exclude <path>" and "root <path>" directives. Relative paths resolve against
    // the root directive, or base_dir without one.
    bool parse_list_stream(std::istream& stream, const fs::path& base_dir) {
        std::string line;
        size_t line_number = 0;
        fs::path list_root;
        while (std::getline(stream, line)) {
            ++line_number;
            std::string trimmed = trim_copy(line);
            if (trimmed.empty() || trimmed.front() == '#') {
                continue;
            }
            const fs::path& base = !list_root.empty() ? list_root : base_dir;
            size_t pos = 0;
            if (match_directive(trimmed, "exclude", pos)) {
                std::string excluded_path_text;
                if (pos < trimmed.size() && trimmed[pos] == '"') {
                    std::string error;
                    if (!sprat::core::parse_quoted(trimmed, pos, excluded_path_text, error)) {
                        std::cerr << tr("Invalid exclude path at line ") << line_number << tr(": ") << error << "\n";
                        return false;
                    }
                } else if (pos < trimmed.size()) {
                    excluded_path_text = trimmed.substr(pos);
                }
                if (excluded_path_text.empty()) {
                    std::cerr << tr("Invalid exclude path at line ") << line_number << tr(": ") << to_quoted(trimmed) << "\n";
                    return false;
                }
                fs::path excluded_path(excluded_path_text);
                if (excluded_path.is_relative()) {
                    excluded_path = base / excluded_path;
                }
                excluded_.add(excluded_path);
                continue;
            }
            if (match_directive(trimmed, "root", pos)) {
                std::string root_str;
                if (pos < trimmed.size() && trimmed[pos] == '"') {
                    std::string error;
                    sprat::core::parse_quoted(trimmed, pos, root_str, error);
                } else if (pos < trimmed.size()) {
                    root_str = trimmed.substr(pos);
                }
                if (!root_str.empty()) {
                    fs::path rp(root_str);
                    if (rp.is_relative()) {
                        rp = base_dir / rp;
                    }
                    list_root = rp;
                }
                continue;
            }
            fs::path entry_path(trimmed);
            if (entry_path.is_relative()) {
                entry_path = base / entry_path;
            }
            if (!fs::exists(entry_path) || !fs::is_regular_file(entry_path)) {
                std::cerr << tr("Invalid image path at line ") << line_number << tr(": ") << to_quoted(trimmed) << "\n";
                return false;
            }
            if (!add_source(entry_path, true)) {
                return false;
            }
        }
        return true;
    }

    // Strict sources (named in a list) must be valid images; others are skipped silently.
    bool add_source(const fs::path& image_path, bool strict) {
        if (!is_supported_image_extension(image_path)) {
            if (strict) {
                std::cerr << tr("Invalid extension in list input: ") << to_quoted(image_path) << "\n";
                return false;
            }
            return true;
        }
        const fs::path* exclusion_root =
            input_.type == InputType::Directory ? &input_.working_folder : nullptr;
        if (excluded_.contains(image_path, exclusion_root)) {
            return true;
        }
        ImageMeta meta;
        if (!read_image_meta(image_path, meta)) {
            if (strict) {
                std::cerr << tr("Failed to stat image: ") << to_quoted(image_path) << "\n";
                return false;
            }
            return true;
        }
        ImageSource source;
        source.file_path = image_path;
        source.path = image_path.string();
        source.meta = meta;
        sources_.push_back(std::move(source));
        return true;
    }

    const InputContext& input_;
    std::vector<ImageSource>& sources_;
    ExclusionSet excluded_;
};

} // namespace

bool open_path_input(const fs::path& input_path, InputContext& out_context) {
    const bool is_dir = fs::exists(input_path) && fs::is_directory(input_path);
    const bool is_file = fs::exists(input_path) && fs::is_regular_file(input_path);
    const bool is_tar = is_file && is_tar_file(input_path);
    const bool is_compressed_tar = is_file && is_compressed_tar_file(input_path);

    if (is_tar || is_compressed_tar) {
        fs::path temp_dir;
        if (!create_extraction_dir("spratlayout_extract_", temp_dir)) {
            std::cerr << tr("Error: Failed to create temporary directory for archive extraction\n");
            return false;
        }
        out_context.temp_dirs_to_cleanup.push_back(temp_dir);

        if (!extract_tar_file(input_path, temp_dir)) {
            std::cerr << tr("Error: Failed to extract archive file: ") << to_quoted(input_path) << "\n";
            return false;
        }

        out_context.type = InputType::TarFile;
        out_context.working_folder = temp_dir;
        return true;
    }
    if (is_dir) {
        out_context.type = InputType::Directory;
        out_context.working_folder = input_path;
        return true;
    }
    if (is_file) {
        out_context.type = InputType::ListFile;
        out_context.working_folder = input_path;
        return true;
    }

    std::cerr << tr("Error: Input path does not exist or is not a file/directory: ")
              << to_quoted(input_path) << "\n";
    return false;
}

bool open_stdin_archive_input(InputContext& out_context) {
    fs::path temp_dir;
    if (!create_extraction_dir("spratlayout_extract_stdin_", temp_dir)) {
        std::cerr << tr("Error: Failed to create temporary directory for stdin tar extraction\n");
        return false;
    }
    out_context.temp_dirs_to_cleanup.push_back(temp_dir);

    if (!extract_tar_from_stdin(temp_dir)) {
        std::cerr << tr("Error: Failed to extract archive from stdin\n");
        return false;
    }

    out_context.type = InputType::StdinTar;
    out_context.working_folder = temp_dir;
    return true;
}

void open_stdin_list_input(InputContext& out_context, const fs::path& cwd) {
    out_context.type = InputType::StdinList;
    out_context.working_folder = cwd;
}

InputContext::~InputContext() {
    for (const auto& dir : temp_dirs_to_cleanup) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}

bool is_extracted_archive(const InputContext& input) {
    return input.type == InputType::TarFile || input.type == InputType::StdinTar;
}

fs::path layout_root(const InputContext& input) {
    return input.type == InputType::ListFile ? input.working_folder.parent_path() : input.working_folder;
}

bool collect_image_sources(const InputContext& input, std::vector<ImageSource>& out) {
    return SourceCollector(input, out).collect();
}

} // namespace sprat::layout
