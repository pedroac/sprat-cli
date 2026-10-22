#include "commands/entrypoints.h"
#include "core/run_main.h"

int main(int argc, char** argv) {
    return sprat::core::run_main(run_spratunpack, argc, argv);
}
