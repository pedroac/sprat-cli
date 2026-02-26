#include "cli_parse.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <cmath>

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

bool parse_int(const std::string& token, int& out) {
    if (token.empty()) {
        return false;
    }
    std::istringstream iss(token);
    int value = 0;
    char extra = '\0';
    if (!(iss >> value)) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    out = value;
    return true;
}

bool parse_double(const std::string& token, double& out) {
    if (token.empty()) {
        return false;
    }
    std::istringstream iss(token);
    double value = 0.0;
    char extra = '\0';
    if (!(iss >> value)) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool parse_pair(const std::string& token, int& a, int& b) {
    size_t comma = token.find(',');
    if (comma == std::string::npos || comma == 0 || comma + 1 >= token.size()) {
        return false;
    }
    if (token.find(',', comma + 1) != std::string::npos) {
        return false;
    }
    return parse_int(token.substr(0, comma), a) && parse_int(token.substr(comma + 1), b);
}

bool parse_quoted(std::string_view input, size_t& pos, std::string& out, std::string& error) {
    if (pos >= input.size() || input[pos] != '"') {
        error = "expected opening quote";
        return false;
    }

    ++pos;
    out.clear();

    while (pos < input.size()) {
        char c = input[pos++];
        if (c == '\\') {
            if (pos >= input.size()) {
                error = "unterminated escape sequence";
                return false;
            }
            char escaped = input[pos++];
            if (escaped == '"' || escaped == '\\') {
                out.push_back(escaped);
            } else {
                out.push_back('\\');
                out.push_back(escaped);
            }
        } else if (c == '"') {
            return true;
        } else {
            out.push_back(c);
        }
    }

    error = "unterminated quoted string";
    return false;
}

std::string to_quoted(const std::string& s) {
    std::string result = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' ) {
            result += '\\';
        }
        result += c;
    }
    result += "\"";
    return result;
}

} // namespace sprat::core
