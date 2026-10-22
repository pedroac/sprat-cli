#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "core/i18n.h"

namespace sprat::core {

// Writes data to path, replacing the file. Fails (setting error) if the file can't be opened
// or the data doesn't fully reach it, e.g. on a full disk; the partial file is then removed
// (if it is a regular file) so no truncated atlas or metadata file is left behind.
inline bool write_file(const std::filesystem::path& path, std::string_view data, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = tr("cannot open file for writing");
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();  // flushes; a failed flush sets failbit
    if (!out) {
        error = tr("write failed (disk full or I/O error)");
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {  // never a device such as /dev/full
            std::filesystem::remove(path, ec);
        }
        return false;
    }
    return true;
}

// Flushes standard output when it carries a command's result (layout text, an atlas, a TAR,
// metadata). Returns false, after reporting it, when the data did not all get through (closed
// pipe, full disk), so the run fails instead of looking successful. Commands call it only for
// that output: a lost progress message on stdout does not make a run fail.
inline bool finish_stdout() {
    std::cout.flush();
    if (!std::cout) {
        std::cerr << tr("Error: failed to write to standard output\n");
        return false;
    }
    return true;
}

} // namespace sprat::core
