#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: compact_seed_search_regression_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if command -v magick >/dev/null; then
    create_image_cmd="magick"
elif command -v convert >/dev/null; then
    create_image_cmd="convert"
else
    echo "Error: ImageMagick not found" >&2
    exit 1
fi

mkdir -p "$tmp_dir/frames"
"$create_image_cmd" -size 60x60 xc:red "$tmp_dir/frames/a.png"
"$create_image_cmd" -size 60x60 xc:red "$tmp_dir/frames/b.png"
"$create_image_cmd" -size 60x60 xc:red "$tmp_dir/frames/c.png"

output_file="$tmp_dir/layout.txt"
if ! "$spratlayout_bin" "$tmp_dir/frames" --mode compact --max-width 120 --max-height 120 > "$output_file"; then
    echo "FAILED: compact layout should succeed with a valid 120x120 packing candidate" >&2
    exit 1
fi

if ! grep -q '^atlas 120,120$' "$output_file"; then
    echo "FAILED: expected atlas 120,120 in compact output" >&2
    cat "$output_file" >&2
    exit 1
fi

echo "Compact seed search regression test passed!"
