#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: unpack_error_test.sh <spratunpack-bin>" >&2
    exit 1
fi

unpack_bin="$1"

tmp_dir="$(mktemp -d)"
trap "rm -rf \"$tmp_dir\"" EXIT

echo "Test 1: spratunpack with missing input"
if "$unpack_bin" missing.png < /dev/null > /dev/null 2>&1; then
    echo "FAILED: spratunpack should fail with missing input"
    exit 1
fi

echo "Test 2: spratunpack with invalid input"
echo "not an image" > "$tmp_dir/invalid.png"
if "$unpack_bin" "$tmp_dir/invalid.png" < /dev/null > /dev/null 2>&1; then
    echo "FAILED: spratunpack should fail with invalid input"
    exit 1
fi

echo "unpack_error_test.sh: ok"
