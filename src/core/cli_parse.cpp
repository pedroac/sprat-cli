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

std::string to_quoted(const char* s) {
    return to_quoted(std::string(s));
}

std::string to_quoted(const std::filesystem::path& p) {
    return to_quoted(p.string());
}

int compare_natural(std::string_view a, std::string_view b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            // Skip leading zeros
            while (i < a.size() && a[i] == '0') i++;
            while (j < b.size() && b[j] == '0') j++;

            size_t start_i = i;
            size_t start_j = j;

            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) i++;
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) j++;

            size_t len_i = i - start_i;
            size_t len_j = j - start_j;

            if (len_i != len_j) {
                return len_i < len_j ? -1 : 1;
            }

            std::string_view num_a = a.substr(start_i, len_i);
            std::string_view num_b = b.substr(start_j, len_j);
            if (num_a != num_b) {
                return num_a < num_b ? -1 : 1;
            }
        } else {
            if (a[i] != b[j]) {
                return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]) ? -1 : 1;
            }
            i++;
            j++;
        }
    }

    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

} // namespace sprat::core
