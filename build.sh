#!/usr/bin/env bash

set -u

if cmake -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master .; then
    if make; then
        ctest --test-dir tests --output-on-failure
    else
        echo "make failed"
        exit 1
    fi
else
    echo "cmake failed"
    exit 1
fi

