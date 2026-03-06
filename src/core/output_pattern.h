#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sprat::core {

bool format_index_pattern(std::string_view pattern,
                          int index,
                          std::string& out,
                          std::string& error,
                          size_t* placeholder_count = nullptr);

bool validate_output_pattern(std::string_view pattern,
                             size_t atlas_count,
                             bool require_placeholder_for_multiple,
                             std::string& error,
                             size_t* placeholder_count = nullptr);

} // namespace sprat::core
