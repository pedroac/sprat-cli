#include "cli_parse.h"

#include <exception>
#include <limits>

namespace sprat::core {

bool parse_positive_int(const std::string& value, int& out) {
    try {
        size_t idx = 0;
        long long parsed = std::stoll(value, &idx);
        if (idx != value.size()
            || parsed <= 0
            || parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_non_negative_int(const std::string& value, int& out) {
    try {
        size_t idx = 0;
        long long parsed = std::stoll(value, &idx);
        if (idx != value.size()
            || parsed < 0
            || parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
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
