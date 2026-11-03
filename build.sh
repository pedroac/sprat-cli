#!/usr/bin/env bash

set -euo pipefail

build_dir="build"

echo "Configuring..."
cmake -S . -B "$build_dir" -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master -DBUILD_TESTING=ON

echo "Building..."
# A bare --parallel means unlimited jobs with Makefiles and can run out of memory.
cmake --build "$build_dir" --parallel "${SPRAT_BUILD_JOBS:-4}"

echo "Testing..."
ctest --test-dir "$build_dir" --output-on-failure
