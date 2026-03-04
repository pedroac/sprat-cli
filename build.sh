#!/usr/bin/env bash

set -euo pipefail

echo "Updating VERSION..."

TAG=$(git ls-remote --tags --sort="v:refname" https://github.com/pedroac/sprat-cli.git 2>/dev/null | tail -n 1 | awk -F/ '{print $3}' | head -n 1)

if [ -z "$TAG" ]; then
    TAG=$(git rev-parse --short HEAD)
fi

echo "$TAG" > VERSION
echo "Version set to: $TAG"

build_dir="build"

echo "Configuring..."
cmake -S . -B "$build_dir" -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master

echo "Building..."
cmake --build "$build_dir" --parallel

echo "Testing..."
ctest --test-dir "$build_dir" --output-on-failure
