#include "layout_parser.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace sprat::core {

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
        error = "expected opening quote for sprite path";
        return false;
    }

    ++pos;
    out.clear();

    while (pos < input.size()) {
        char c = input[pos++];
        if (c == '\\') {
            if (pos >= input.size()) {
                error = "unterminated escape sequence in sprite path";
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

    error = "unterminated quoted sprite path";
    return false;
}

bool parse_sprite_line(const std::string& line, Sprite& out, std::string& error) {
    constexpr std::string_view prefix = "sprite";
    if (!line.starts_with(prefix)) {
        error = "line does not start with sprite";
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    std::string path;
    if (pos >= line.size() || line[pos] != '"') {
        error = "sprite path must be quoted";
        return false;
    }

    if (!parse_quoted(line, pos, path, error)) {
        return false;
    }

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    Sprite parsed;
    parsed.path = path;
    std::vector<std::string> tokens;
    std::istringstream tail(line.substr(pos));
    std::string token;
    while (tail >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        error = "sprite line is missing numeric fields";
        return false;
    }

    bool rotated = false;
    if (!tokens.empty() && tokens.back() == "rotated") {
        rotated = true;
        tokens.pop_back();
    }

    if (tokens.empty()) {
        error = "sprite line is missing numeric fields";
        return false;
    }

    if (tokens[0].find(',') != std::string::npos) {
        constexpr size_t MODERN_SPRITE_TOKENS_MIN = 2;
        constexpr size_t MODERN_SPRITE_TOKENS_MAX = 4;
        if (tokens.size() != MODERN_SPRITE_TOKENS_MIN && tokens.size() != MODERN_SPRITE_TOKENS_MAX) {
            error = "sprite line must contain position/size and optional trim offsets";
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y) || !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = "invalid position or size pair";
            return false;
        }

        if (tokens.size() == MODERN_SPRITE_TOKENS_MAX) {
            if (!parse_pair(tokens[2], parsed.src_x, parsed.src_y)
                || !parse_pair(tokens[3], parsed.trim_right, parsed.trim_bottom)) {
                error = "invalid trim offset pair";
                return false;
            }
            parsed.has_trim = true;
        }
    } else {
        constexpr size_t LEGACY_SPRITE_TOKENS_MIN = 4;
        constexpr size_t LEGACY_SPRITE_TOKENS_MAX = 6;
        if (tokens.size() != LEGACY_SPRITE_TOKENS_MIN && tokens.size() != LEGACY_SPRITE_TOKENS_MAX) {
            error = "legacy sprite line has invalid field count";
            return false;
        }
        if (!parse_int(tokens[0], parsed.x)
            || !parse_int(tokens[1], parsed.y)
            || !parse_int(tokens[2], parsed.w)
            || !parse_int(tokens[3], parsed.h)) {
            error = "legacy sprite line has invalid numeric fields";
            return false;
        }
        if (tokens.size() == LEGACY_SPRITE_TOKENS_MAX) {
            if (!parse_int(tokens[4], parsed.src_x) || !parse_int(tokens[5], parsed.src_y)) {
                error = "legacy sprite line has invalid crop offsets";
                return false;
            }
            parsed.has_trim = true;
        }
    }

    parsed.rotated = rotated;
    out = parsed;
    return true;
}

bool parse_atlas_line(const std::string& line, int& width, int& height) {
    std::istringstream iss(line);
    std::string tag;
    std::string size_token;
    std::string extra;

    if (!(iss >> tag >> size_token)) {
        return false;
    }
    if (tag != "atlas") {
        return false;
    }
    if (!parse_pair(size_token, width, height)) {
        // Backward compatibility: atlas <w> <h>
        if (!parse_int(size_token, width) || !(iss >> height)) {
            return false;
        }
    }
    if (iss >> extra) {
        return false;
    }

    return true;
}

bool parse_scale_line(const std::string& line, double& scale) {
    std::istringstream iss(line);
    std::string tag;
    std::string value_token;
    std::string extra;

    if (!(iss >> tag >> value_token)) {
        return false;
    }
    if (tag != "scale") {
        return false;
    }
    if (!parse_double(value_token, scale) || scale <= 0.0) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return true;
}

bool parse_layout(std::istream& in, Layout& out, std::string& error) {
    Layout parsed;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (line.starts_with("atlas")) {
            if (!parse_atlas_line(line, parsed.atlas_width, parsed.atlas_height)) {
                error = "Invalid atlas line: " + line;
                return false;
            }
        } else if (line.starts_with("scale")) {
            if (parsed.has_scale) {
                error = "Duplicate scale line";
                return false;
            }
            if (!parse_scale_line(line, parsed.scale)) {
                error = "Invalid scale line: " + line;
                return false;
            }
            parsed.has_scale = true;
        } else if (line.starts_with("sprite")) {
            Sprite s;
            std::string sprite_error;
            if (!parse_sprite_line(line, s, sprite_error)) {
                error = "Invalid sprite line: " + sprite_error;
                return false;
            }
            parsed.sprites.push_back(s);
        } else {
            error = "Unknown line: " + line;
            return false;
        }
    }

    if (parsed.atlas_width <= 0 || parsed.atlas_height <= 0) {
        error = "Invalid atlas size";
        return false;
    }

    if (!parsed.has_scale) {
        parsed.scale = 1.0;
    }

    out = std::move(parsed);
    return true;
}

} // namespace sprat::core
