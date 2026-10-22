#include "i18n.h"

#include <cstdlib>

#ifdef SPRAT_ENABLE_GETTEXT
#include <clocale>
#include <libintl.h>
#endif

#ifndef SPRAT_LOCALE_DIR
#define SPRAT_LOCALE_DIR "/usr/share/locale"
#endif

namespace sprat::core {

void init_i18n(const char* domain) {
#ifdef SPRAT_ENABLE_GETTEXT
    std::setlocale(LC_ALL, "");
    // Layouts, metadata and option values always use '.' decimals; a user locale such as
    // pt_PT or de_DE would make strtod/snprintf read and write ',' instead.
    std::setlocale(LC_NUMERIC, "C");
    const char* env_locale_dir = std::getenv("SPRAT_LOCALE_DIR");
    const char* locale_dir = (env_locale_dir != nullptr && env_locale_dir[0] != '\0')
        ? env_locale_dir
        : SPRAT_LOCALE_DIR;
    bindtextdomain(domain, locale_dir);
    bind_textdomain_codeset(domain, "UTF-8");
    textdomain(domain);
#else
    (void)domain;
#endif
}

const char* tr(const char* msgid) {
#ifdef SPRAT_ENABLE_GETTEXT
    return gettext(msgid);
#else
    return msgid;
#endif
}

} // namespace sprat::core
