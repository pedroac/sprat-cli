#!/usr/bin/env bash
# Rotated sprites are turned clockwise by default and counter-clockwise with "rotation ccw"
# (libGDX); spratunpack must undo either direction exactly.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: rotation_direction_test.sh <spratpack-bin> <spratunpack-bin>" >&2
    exit 1
fi

pack_bin="$1"
unpack_bin="$2"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "rotation_direction_test.sh: skipped (python with Pillow not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# 20x10 source: left half red, right half green, top-left pixel blue.
"$python_bin" - "$(fix_path "$tmp_dir/src.png")" <<'EOF'
import sys
from PIL import Image
im = Image.new("RGBA", (20, 10))
for x in range(20):
    for y in range(10):
        im.putpixel((x, y), (255, 0, 0, 255) if x < 10 else (0, 255, 0, 255))
im.putpixel((0, 0), (0, 0, 255, 255))
im.save(sys.argv[1])
EOF

# direction, pixel where the source's top-left must land in the 10x20 slot
for case in "cw 9,0" "ccw 0,19"; do
    set -- $case
    direction="$1"
    expected="$2"
    echo "Test: rotation $direction"
    layout="$tmp_dir/layout_$direction.txt"
    {
        echo "root \"$(fix_path "$tmp_dir")\""
        echo "rotation $direction"
        echo "atlas 10,20"
        echo "sprite \"src.png\" 0,0 10,20 rotated"
    } > "$layout"
    "$pack_bin" < "$layout" > "$tmp_dir/atlas_$direction.png"
    "$unpack_bin" "$(fix_path "$tmp_dir/atlas_$direction.png")" -f "$(fix_path "$layout")" \
        -o "$(fix_path "$tmp_dir/out_$direction")" > /dev/null
    "$python_bin" - "$(fix_path "$tmp_dir/atlas_$direction.png")" "$expected" \
        "$(fix_path "$tmp_dir/src.png")" "$(fix_path "$tmp_dir/out_$direction/src.png")" <<'EOF'
import sys
from PIL import Image
atlas = Image.open(sys.argv[1]).convert("RGBA")
x, y = (int(v) for v in sys.argv[2].split(","))
if atlas.getpixel((x, y)) != (0, 0, 255, 255):
    sys.exit("FAILED: source top-left pixel is not at (%d, %d) in the atlas" % (x, y))
src = Image.open(sys.argv[3]).convert("RGBA")
out = Image.open(sys.argv[4]).convert("RGBA")
if src.size != out.size or src.tobytes() != out.tobytes():
    sys.exit("FAILED: unpacked sprite differs from the source")
EOF
done

echo "rotation_direction_test.sh: ok"
