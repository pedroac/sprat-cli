#include "cli_parse.h"

#include <charconv>
#include <limits>

namespace sprat::core {

bool parse_positive_int(const std::string& value, int& out) {
    int parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }
    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_non_negative_int(const std::string& value, int& out) {
    int parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }
    if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_non_negative_uint(const std::string& value, unsigned int& out) {
    int parsed = 0;
    if (!parse_non_negative_int(value, parsed)) {
        return false;
    }
    out = static_cast<unsigned int>(parsed);
    return true;
}

bool parse_positive_uint(const std::string& value, unsigned int& out) {
    int parsed = 0;
    if (!parse_positive_int(value, parsed)) {
        return false;
    }
    out = static_cast<unsigned int>(parsed);
    return true;
}

} // namespace sprat::core
