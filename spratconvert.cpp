#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct Sprite {
    std::string path;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int src_x = 0;
    int src_y = 0;
    int trim_right = 0;
    int trim_bottom = 0;
    bool has_trim = false;
};

struct Layout {
    int atlas_width = 0;
    int atlas_height = 0;
    double scale = 1.0;
    bool has_scale = false;
    std::vector<Sprite> sprites;
};

struct Transform {
    std::string name;
    std::string description;
    std::string extension;
    std::string header;
    std::string sprite;
    std::string separator;
    std::string footer;
};

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }

    return s.substr(start, end - start);
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
    out = value;
    return std::isfinite(value);
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
    if (line.rfind(prefix, 0) != 0) {
        error = "line does not start with sprite";
        return false;
    }

    size_t pos = prefix.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }

    std::string path;
    if (pos < line.size() && line[pos] == '"') {
        if (!parse_quoted(line, pos, path, error)) {
            return false;
        }
    } else {
        error = "sprite path must be quoted";
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

    if (tokens[0].find(',') != std::string::npos) {
        if (tokens.size() != 2 && tokens.size() != 4) {
            error = "sprite line must contain position/size and optional trim offsets";
            return false;
        }

        if (!parse_pair(tokens[0], parsed.x, parsed.y) ||
            !parse_pair(tokens[1], parsed.w, parsed.h)) {
            error = "invalid position or size pair";
            return false;
        }

        if (tokens.size() == 4) {
            if (!parse_pair(tokens[2], parsed.src_x, parsed.src_y) ||
                !parse_pair(tokens[3], parsed.trim_right, parsed.trim_bottom)) {
                error = "invalid trim offset pair";
                return false;
            }
            parsed.has_trim = true;
        }
    } else {
        if (tokens.size() != 4 && tokens.size() != 6) {
            error = "legacy sprite line has invalid field count";
            return false;
        }
        if (!parse_int(tokens[0], parsed.x) ||
            !parse_int(tokens[1], parsed.y) ||
            !parse_int(tokens[2], parsed.w) ||
            !parse_int(tokens[3], parsed.h)) {
            error = "legacy sprite line has invalid numeric fields";
            return false;
        }
        if (tokens.size() == 6) {
            if (!parse_int(tokens[4], parsed.src_x) ||
                !parse_int(tokens[5], parsed.src_y)) {
                error = "legacy sprite line has invalid crop offsets";
                return false;
            }
            parsed.has_trim = true;
        }
    }

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

        if (line.rfind("atlas", 0) == 0) {
            if (!parse_atlas_line(line, parsed.atlas_width, parsed.atlas_height)) {
                error = "Invalid atlas line: " + line;
                return false;
            }
        } else if (line.rfind("scale", 0) == 0) {
            if (parsed.has_scale) {
                error = "Duplicate scale line";
                return false;
            }
            if (!parse_scale_line(line, parsed.scale)) {
                error = "Invalid scale line: " + line;
                return false;
            }
            parsed.has_scale = true;
        } else if (line.rfind("sprite", 0) == 0) {
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

std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u";
                    const char* digits = "0123456789abcdef";
                    unsigned char uc = static_cast<unsigned char>(c);
                    hex << '0' << '0' << digits[(uc >> 4) & 0x0f] << digits[uc & 0x0f];
                    out += hex.str();
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string escape_xml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string escape_csv(const std::string& s) {
    bool needs_quotes = false;
    for (char c : s) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return s;
    }

    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string escape_css_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        if (c == '\n') {
            out += "\\a ";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string replace_tokens(const std::string& input, const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(input.size() + 64);

    size_t i = 0;
    while (i < input.size()) {
        size_t open = input.find("{{", i);
        if (open == std::string::npos) {
            out.append(input.substr(i));
            break;
        }

        out.append(input.substr(i, open - i));
        size_t close = input.find("}}", open + 2);
        if (close == std::string::npos) {
            out.append(input.substr(open));
            break;
        }

        std::string key = trim_copy(input.substr(open + 2, close - (open + 2)));
        auto it = vars.find(key);
        if (it != vars.end()) {
            out.append(it->second);
        }
        i = close + 2;
    }

    return out;
}

bool parse_transform_file(const fs::path& path, Transform& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "Failed to open transform file: " + path.string();
        return false;
    }

    Transform parsed;
    std::string section;
    std::string line;

    auto append_line = [](std::string& target, const std::string& value) {
        if (!target.empty()) {
            target.push_back('\n');
        }
        target.append(value);
    };

    while (std::getline(in, line)) {
        if (line.empty() && section.empty()) {
            continue;
        }

        std::string trimmed = trim_copy(line);
        if (trimmed.empty() && section.empty()) {
            continue;
        }

        if (!trimmed.empty() && trimmed.front() == '#') {
            continue;
        }

        if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        if (section == "meta") {
            size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = trim_copy(line.substr(0, eq));
            std::string value = trim_copy(line.substr(eq + 1));
            if (key == "name") {
                parsed.name = value;
            } else if (key == "description") {
                parsed.description = value;
            } else if (key == "extension") {
                parsed.extension = value;
            }
            continue;
        }

        if (section == "header") {
            append_line(parsed.header, line);
        } else if (section == "sprites" || section == "sprite") {
            append_line(parsed.sprite, line);
        } else if (section == "separator") {
            append_line(parsed.separator, line);
        } else if (section == "footer") {
            append_line(parsed.footer, line);
        }
    }

    if (parsed.name.empty()) {
        parsed.name = path.stem().string();
    }
    if (parsed.sprite.empty()) {
        error = "Transform missing [sprites] section: " + path.string();
        return false;
    }

    out = std::move(parsed);
    return true;
}

std::string format_double(double value) {
    std::ostringstream oss;
    oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
    oss.precision(8);
    oss << value;
    return oss.str();
}

fs::path find_transforms_dir(const std::string& argv0) {
    std::vector<fs::path> candidates;
    candidates.push_back(fs::path("transforms"));
#ifdef SPRAT_SOURCE_DIR
    candidates.push_back(fs::path(SPRAT_SOURCE_DIR) / "transforms");
#endif
    if (!argv0.empty()) {
        fs::path exe_dir = fs::path(argv0).parent_path();
        if (!exe_dir.empty()) {
            candidates.push_back(exe_dir / "transforms");
            candidates.push_back(exe_dir / ".." / "transforms");
            candidates.push_back(exe_dir / ".." / ".." / "transforms");
        }
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return fs::path("transforms");
}

fs::path resolve_transform_path(const std::string& transform_arg, const std::string& argv0) {
    fs::path candidate(transform_arg);
    if (candidate.has_parent_path() || candidate.extension() == ".transform") {
        return candidate;
    }
    return find_transforms_dir(argv0) / (transform_arg + ".transform");
}

bool load_transform_by_name(const std::string& transform_arg, const std::string& argv0, Transform& out, std::string& error) {
    fs::path transform_path = resolve_transform_path(transform_arg, argv0);
    return parse_transform_file(transform_path, out, error);
}

void list_transforms(const std::string& argv0) {
    const fs::path dir = find_transforms_dir(argv0);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".transform") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths) {
        Transform t;
        std::string error;
        if (!parse_transform_file(path, t, error)) {
            std::cerr << "Warning: " << error << "\n";
            continue;
        }
        std::cout << t.name;
        if (!t.description.empty()) {
            std::cout << " - " << t.description;
        }
        std::cout << "\n";
    }
}

void print_usage() {
    std::cerr << "Usage: spratconvert [--transform NAME|PATH] [--list-transforms]\n";
}

int main(int argc, char** argv) {
    std::string transform_arg = "json";
    bool list_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--transform" && i + 1 < argc) {
            transform_arg = argv[++i];
        } else if (arg == "--list-transforms") {
            list_only = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            print_usage();
            return 1;
        }
    }

    if (list_only) {
        list_transforms(argv[0] ? argv[0] : "");
        return 0;
    }

    Transform transform;
    std::string transform_error;
    if (!load_transform_by_name(transform_arg, argv[0] ? argv[0] : "", transform, transform_error)) {
        std::cerr << transform_error << "\n";
        return 1;
    }

    Layout layout;
    std::string layout_error;
    if (!parse_layout(std::cin, layout, layout_error)) {
        std::cerr << layout_error << "\n";
        return 1;
    }

    std::map<std::string, std::string> global_vars;
    global_vars["atlas_width"] = std::to_string(layout.atlas_width);
    global_vars["atlas_height"] = std::to_string(layout.atlas_height);
    global_vars["scale"] = format_double(layout.scale);
    global_vars["sprite_count"] = std::to_string(layout.sprites.size());

    if (!transform.header.empty()) {
        std::cout << replace_tokens(transform.header, global_vars);
    }

    for (size_t i = 0; i < layout.sprites.size(); ++i) {
        if (i > 0 && !transform.separator.empty()) {
            std::cout << replace_tokens(transform.separator, global_vars);
        }

        const Sprite& s = layout.sprites[i];
        std::map<std::string, std::string> vars = global_vars;
        vars["index"] = std::to_string(i);
        vars["path"] = s.path;
        vars["path_json"] = escape_json(s.path);
        vars["path_xml"] = escape_xml(s.path);
        vars["path_csv"] = escape_csv(s.path);
        vars["path_css"] = escape_css_string(s.path);
        vars["x"] = std::to_string(s.x);
        vars["y"] = std::to_string(s.y);
        vars["w"] = std::to_string(s.w);
        vars["h"] = std::to_string(s.h);
        vars["src_x"] = std::to_string(s.src_x);
        vars["src_y"] = std::to_string(s.src_y);
        vars["trim_right"] = std::to_string(s.trim_right);
        vars["trim_bottom"] = std::to_string(s.trim_bottom);
        vars["has_trim"] = s.has_trim ? "true" : "false";

        std::cout << replace_tokens(transform.sprite, vars);
    }

    if (!transform.footer.empty()) {
        std::cout << replace_tokens(transform.footer, global_vars);
    }

    return 0;
}
