#pragma once

#include <string>
#include <string_view>

namespace sprat::core {

bool parse_positive_int(const std::string& value, int& out);
bool parse_non_negative_int(const std::string& value, int& out);
bool parse_non_negative_uint(const std::string& value, unsigned int& out);
bool parse_positive_uint(const std::string& value, unsigned int& out);

bool parse_int(const std::string& token, int& out);
bool parse_double(const std::string& token, double& out);
bool parse_pair(const std::string& token, int& a, int& b);

bool parse_quoted(std::string_view input, size_t& pos, std::string& out, std::string& error);

std::string to_quoted(const std::string& s);

} // namespace sprat::core
