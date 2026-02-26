#pragma once

#include <string>

namespace sprat::core {

bool parse_positive_int(const std::string& value, int& out);
bool parse_non_negative_int(const std::string& value, int& out);
bool parse_non_negative_uint(const std::string& value, unsigned int& out);
bool parse_positive_uint(const std::string& value, unsigned int& out);

} // namespace sprat::core
