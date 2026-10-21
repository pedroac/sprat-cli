#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: output_scale_test.sh <spratpack-bin>" >&2
    exit 1
fi

spratpack_bin="$1"

if ! command -v python3 >/dev/null 2>&1 || ! python3 -c "import PIL" >/dev/null 2>&1; then
    echo "output_scale_test.sh: skipped (python3 with PIL not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

# Path conversion for Windows
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    tmp_dir_win="$(cygpath -m "$tmp_dir")"
    fix_path() {
        echo "${1/$tmp_dir/$tmp_dir_win}"
    }
else
    fix_path() {
        echo "$1"
    }
fi

frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"

python3 - <<EOF
from PIL import Image
Image.new('RGBA', (5, 2), (0, 255, 0, 255)).save('$(fix_path "$frames_dir/green5x2.png")')
Image.new('RGBA', (5, 2), (255, 0, 0, 255)).save('$(fix_path "$frames_dir/red5x2.png")')
Image.new('RGBA', (4, 4), (0, 255, 0, 255)).save('$(fix_path "$frames_dir/green4x4.png")')
Image.new('RGBA', (4, 4), (255, 0, 0, 255)).save('$(fix_path "$frames_dir/red4x4.png")')
EOF

# Regression: a sprite at an odd offset flush with the atlas edge must stay in
# bounds at a fractional scale (rounding start and width separately overflowed).
layout_file="$tmp_dir/edge_layout.txt"
cat > "$layout_file" <<EOF
atlas 10,2
scale 1
sprite "$(fix_path "$frames_dir/green5x2.png")" 0,0 5,2
sprite "$(fix_path "$frames_dir/red5x2.png")" 5,0 5,2
EOF

edge_sheet="$tmp_dir/edge.png"
if ! "$spratpack_bin" --output-scale 0.5 < "$layout_file" > "$edge_sheet"; then
    echo "spratpack failed on edge-flush sprite at --output-scale 0.5" >&2
    exit 1
fi

python3 - <<EOF
import sys
from PIL import Image
img = Image.open("$(fix_path "$edge_sheet")").convert("RGBA")
if img.size != (5, 1):
    print(f"Invalid scaled atlas size: {img.size}")
    sys.exit(1)
expected = [(0, 255, 0, 255)] * 3 + [(255, 0, 0, 255)] * 2
for x, want in enumerate(expected):
    got = img.getpixel((x, 0))
    if got != want:
        print(f"Pixel at {x},0 is {got}, expected {want}")
        sys.exit(1)
EOF
echo "Edge-flush scaling check passed"

# Regression: the layout's extrude must shrink with the atlas, otherwise it
# overwrites the edge of the neighbouring sprite.
layout_file="$tmp_dir/extrude_layout.txt"
cat > "$layout_file" <<EOF
atlas 16,4
scale 1
extrude 2
sprite "$(fix_path "$frames_dir/green4x4.png")" 2,0 4,4
sprite "$(fix_path "$frames_dir/red4x4.png")" 10,0 4,4
EOF

extrude_sheet="$tmp_dir/extrude.png"
"$spratpack_bin" --output-scale 0.5 < "$layout_file" > "$extrude_sheet"

python3 - <<EOF
import sys
from PIL import Image
img = Image.open("$(fix_path "$extrude_sheet")").convert("RGBA")
if img.size != (8, 2):
    print(f"Invalid scaled atlas size: {img.size}")
    sys.exit(1)
green, red = (0, 255, 0, 255), (255, 0, 0, 255)
# Scaled slots: green [1,3), red [5,7); scaled extrude 1 fills x=0,3 and x=4,7.
expected = [green, green, green, green, red, red, red, red]
for y in range(2):
    for x, want in enumerate(expected):
        got = img.getpixel((x, y))
        if got != want:
            print(f"Pixel at {x},{y} is {got}, expected {want}")
            sys.exit(1)
EOF
echo "Scaled extrude check passed"

# Regression: hostile layouts must be rejected cleanly (exit 1), not crash.
# expect_reject NAME LAYOUT_FILE [SPRATPACK_ARGS...]
expect_reject() {
    local name="$1" layout="$2"
    shift 2
    local status=0
    "$spratpack_bin" "$@" < "$layout" > "$tmp_dir/reject.png" 2> "$tmp_dir/reject.err" || status=$?
    if [ "$status" -ne 1 ]; then
        echo "$name: expected exit 1, got $status" >&2
        cat "$tmp_dir/reject.err" >&2
        exit 1
    fi
}

# Scaled atlas width exceeds INT_MAX; the sprite sits at the far edge.
layout_file="$tmp_dir/overflow_scaled.txt"
cat > "$layout_file" <<EOF
atlas 1952257862,1
sprite "$(fix_path "$frames_dir/red5x2.png")" 1952257861,0 1,1
EOF
expect_reject "scaled atlas overflow" "$layout_file" --output-scale 1.1

# x + w overflows int and wrapped past the layout parser's bounds check.
layout_file="$tmp_dir/overflow_parser.txt"
cat > "$layout_file" <<EOF
atlas 16,16
sprite "$(fix_path "$frames_dir/red5x2.png")" 2147483647,0 1,1
EOF
expect_reject "layout x+w overflow" "$layout_file"

layout_file="$tmp_dir/edge_layout.txt"
for bad_scale in nan inf -1 0; do
    expect_reject "--output-scale $bad_scale" "$layout_file" --output-scale "$bad_scale"
done
echo "Overflow rejection checks passed"

echo "output_scale_test.sh: ok"
