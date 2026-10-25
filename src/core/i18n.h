#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace sprat::core {

void init_i18n(const char* domain);
const char* tr(const char* msgid);

// Translates msgid, then replaces each "{N}" with args[N]. Numbered placeholders let a
// translation reorder the values, which concatenated tr() fragments cannot.
std::string tr_format(const char* msgid, std::initializer_list<std::string_view> args);

} // namespace sprat::core
