#!/usr/bin/env bash
# Regression: when output can't be fully written (full disk, closed pipe), the tools must
# exit with an error instead of reporting success with a truncated atlas or metadata file.
# Every write to /dev/full fails with ENOSPC, standing in for a full disk.
# Exits 77 (skipped) where /dev/full does not exist (macOS, Windows).
set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "Usage: write_failure_test.sh <spratlayout-bin> <spratpack-bin> <spratconvert-bin> <spratunpack-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratpack_bin="$2"
spratconvert_bin="$3"
spratunpack_bin="$4"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
frames_dir="$script_dir/assets/Crocodile Mascot Running Game/transparent PNG/run"

if [ ! -c /dev/full ]; then
    echo "write_failure test skipped: /dev/full is not available"
    exit 77
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

mkdir -p "$tmp_dir/cache"
export TMPDIR="$tmp_dir/cache"
export HOME="$tmp_dir"
export XDG_CONFIG_HOME="$tmp_dir/.config"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# Runs a command that must fail because its output can't be written.
expect_write_failure() {
    local label="$1"
    shift
    local status=0
    "$@" 2> "$tmp_dir/err.txt" || status=$?
    if [ "$status" -eq 0 ]; then
        fail "$label exited 0 although its output could not be written"
    fi
    if ! grep -qiE 'write|disk full' "$tmp_dir/err.txt"; then
        cat "$tmp_dir/err.txt" >&2
        fail "$label did not report the write error"
    fi
}

"$spratlayout_bin" "$frames_dir" > "$tmp_dir/layout.txt"
"$spratpack_bin" < "$tmp_dir/layout.txt" > "$tmp_dir/atlas.png"
"$spratconvert_bin" --transform json < "$tmp_dir/layout.txt" > "$tmp_dir/layout.json"

expect_write_failure "spratlayout to stdout" \
    sh -c '"$0" "$1" > /dev/full' "$spratlayout_bin" "$frames_dir"
expect_write_failure "spratpack to stdout" \
    sh -c '"$0" < "$1" > /dev/full' "$spratpack_bin" "$tmp_dir/layout.txt"
expect_write_failure "spratpack --output file" \
    sh -c '"$0" --output /dev/full < "$1"' "$spratpack_bin" "$tmp_dir/layout.txt"
expect_write_failure "spratconvert to stdout" \
    sh -c '"$0" --transform json < "$1" > /dev/full' "$spratconvert_bin" "$tmp_dir/layout.txt"
expect_write_failure "spratunpack TAR to stdout" \
    sh -c '"$0" "$1" --frames "$2" > /dev/full' "$spratunpack_bin" "$tmp_dir/atlas.png" "$tmp_dir/layout.txt"

# The device must still be there: a failed write only removes regular files.
[ -c /dev/full ] || fail "/dev/full was removed"

echo "write_failure test passed"
