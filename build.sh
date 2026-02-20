#!/bin/bash
set -e

echo "Configuring..."
cmake -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master .

echo "Building..."
make

echo "Testing..."
ctest --test-dir tests --output-on-failure