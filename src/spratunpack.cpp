#include "commands/entrypoints.h"
#include "core/i18n.h"

int main(int argc, char** argv) {
    sprat::core::init_i18n("sprat-cli");
    return run_spratunpack(argc, argv);
}
