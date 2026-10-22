#pragma once

#include <exception>
#include <iostream>

#include "core/i18n.h"

namespace sprat::core {

// Runs a tool's command as main() does. An exception nothing else handled (e.g. a
// filesystem or allocation error) ends the run with an error message and exit code 1
// instead of std::terminate.
inline int run_main(int (*command)(int, char**), int argc, char** argv) {
    init_i18n("sprat-cli");
    try {
        const int status = command(argc, argv);
        // Layouts, atlases and metadata go to stdout; a pipe or disk that took only part of
        // them (closed pipe, full disk) must not look like success.
        std::cout.flush();
        if (status == 0 && !std::cout) {
            std::cerr << tr("Error: failed to write to standard output\n");
            return 1;
        }
        return status;
    } catch (const std::exception& e) {
        std::cerr << tr("Error: ") << e.what() << "\n";
    } catch (...) {
        std::cerr << tr("Error: unexpected failure\n");
    }
    return 1;
}

} // namespace sprat::core
