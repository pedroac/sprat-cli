#!/usr/bin/env bash
# Regression: a subfolder that can't be read is reported and skipped; spratlayout still
# lays out the other images instead of aborting with an uncaught filesystem exception.
# Exits 77 (skipped) where folder permissions can't be taken away (Windows, root).
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratlayout_unreadable_dir_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
frames_dir="$script_dir/assets/Crocodile Mascot Running Game/transparent PNG/run"

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    echo "spratlayout_unreadable_dir test skipped: chmod does not restrict folders on Windows"
    exit 77
fi
if [ "$(id -u)" -eq 0 ]; then
    echo "spratlayout_unreadable_dir test skipped: root can read any folder"
    exit 77
fi

tmp_dir="$(mktemp -d)"
input_dir="$tmp_dir/input"
locked_dir="$input_dir/locked"
trap 'chmod 755 "$locked_dir" 2>/dev/null || true; rm -rf "$tmp_dir"' EXIT

mkdir -p "$tmp_dir/cache"
export TMPDIR="$tmp_dir/cache"
export HOME="$tmp_dir"
export XDG_CONFIG_HOME="$tmp_dir/.config"

mkdir -p "$locked_dir"
cp "$frames_dir/frame-1.png" "$frames_dir/frame-2.png" "$input_dir/"
cp "$frames_dir/frame-3.png" "$locked_dir/"
chmod 000 "$locked_dir"

status=0
"$spratlayout_bin" "$input_dir" > "$tmp_dir/layout.txt" 2> "$tmp_dir/layout.err" || status=$?
if [ "$status" -ne 0 ]; then
    cat "$tmp_dir/layout.err" >&2
    echo "FAIL: spratlayout exited with $status on an unreadable subfolder" >&2
    exit 1
fi
if ! grep -q 'Skipping unreadable folder' "$tmp_dir/layout.err"; then
    cat "$tmp_dir/layout.err" >&2
    echo "FAIL: no warning about the unreadable subfolder" >&2
    exit 1
fi
if [ "$(grep -c '^sprite ' "$tmp_dir/layout.txt")" -ne 2 ]; then
    cat "$tmp_dir/layout.txt" >&2
    echo "FAIL: expected the 2 readable sprites in the layout" >&2
    exit 1
fi

echo "spratlayout_unreadable_dir test passed"
