#include "core/output_path.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "core/i18n.h"

namespace sprat::core {

namespace {

bool is_invalid_filename_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
           c == '"' || c == '<' || c == '>' || c == '|';
}

std::string upper_ascii(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool has_trailing_dot_or_space(std::string_view name) {
    return !name.empty() && (name.back() == '.' || name.back() == ' ');
}

} // namespace

bool is_windows_reserved_name(std::string_view name) {
    const size_t dot = name.find('.');
    const std::string stem = upper_ascii(std::string(name.substr(0, dot)));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

bool is_safe_output_filename(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.find("..") != std::string_view::npos || has_trailing_dot_or_space(name) ||
        is_windows_reserved_name(name)) {
        return false;
    }
    return std::ranges::none_of(name, is_invalid_filename_char);
}

bool is_safe_relative_output_path(std::string_view path, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = tr("path is empty");
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        error = tr("path must be relative");
        return false;
    }
    if (path.find('\\') != std::string_view::npos) {
        error = tr("use '/' as the folder separator");
        return false;
    }
    if (path.find(':') != std::string_view::npos) {
        error = tr("':' is not allowed in a file path");
        return false;
    }

    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string_view segment =
            path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (segment.empty()) {
            error = tr("path has an empty folder or file name");
            return false;
        }
        if (segment == "." || segment == "..") {
            error = tr("path must not contain '.' or '..' segments");
            return false;
        }
        if (has_trailing_dot_or_space(segment)) {
            error = tr("a folder or file name ends with a dot or space");
            return false;
        }
        if (is_windows_reserved_name(segment)) {
            error = tr("a folder or file name is reserved on Windows");
            return false;
        }
        if (std::ranges::any_of(segment, is_invalid_filename_char)) {
            error = tr("path contains a character that is invalid in a file name");
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

} // namespace sprat::core
