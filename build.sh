#!/usr/bin/env bash

set -euo pipefail
echo "Configuring..."
cmake -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master .

echo "Building..."
cmake --build . --parallel

echo "Testing..."
ctest --test-dir tests --output-on-failure
