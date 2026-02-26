#!/usr/bin/env bash

set -euo pipefail
build_dir="build"

echo "Configuring..."
cmake -S . -B "$build_dir" -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master

echo "Building..."
cmake --build "$build_dir" --parallel

echo "Testing..."
ctest --test-dir "$build_dir" --output-on-failure
