#include "output_pattern.h"

namespace sprat::core {

bool format_index_pattern(std::string_view pattern,
                          int index,
                          std::string& out,
                          std::string& error,
                          size_t* placeholder_count) {
    out.clear();
    error.clear();
    size_t replaced = 0;
    out.reserve(pattern.size() + 8);

    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c != '%') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= pattern.size()) {
            error = "trailing '%' in output pattern";
            return false;
        }

        const char spec = pattern[++i];
        if (spec == '%') {
            out.push_back('%');
            continue;
        }
        if (spec == 'd') {
            out += std::to_string(index);
            ++replaced;
            continue;
        }

        error = "unsupported placeholder '%";
        error.push_back(spec);
        error += "' (only %d and %% are supported)";
        return false;
    }

    if (placeholder_count != nullptr) {
        *placeholder_count = replaced;
    }
    return true;
}

bool validate_output_pattern(std::string_view pattern,
                             size_t atlas_count,
                             bool require_placeholder_for_multiple,
                             std::string& error,
                             size_t* placeholder_count) {
    error.clear();
    if (placeholder_count != nullptr) {
        *placeholder_count = 0;
    }
    if (pattern.empty()) {
        return true;
    }

    std::string sample;
    size_t replaced = 0;
    if (!format_index_pattern(pattern, 0, sample, error, &replaced)) {
        return false;
    }

    if (require_placeholder_for_multiple && atlas_count > 1 && replaced == 0) {
        error = "must include %d when layout has multiple atlases";
        return false;
    }

    if (placeholder_count != nullptr) {
        *placeholder_count = replaced;
    }
    return true;
}

} // namespace sprat::core
