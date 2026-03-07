#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: extrude_test.sh <spratlayout-bin> <spratpack-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratpack_bin="$2"

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

# Create a 2x2 red pixel PNG (solid)
cat > "$tmp_dir/red2x2.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVR4nGP8z8Dwn4GBgYGJAQoAHxcCAk+Uzr4AAAAASUVORK5CYII=
EOF

if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/red2x2.b64" > "$frames_dir/red.png"
else
    base64 -D -i "$tmp_dir/red2x2.b64" -o "$frames_dir/red.png"
fi

layout_file="$tmp_dir/layout.txt"
# Use --extrude 1. Padding will be auto-set to 2.
"$spratlayout_bin" "$(fix_path "$frames_dir")" --extrude 1 --mode fast > "$layout_file"

if ! grep -q "^extrude 1$" "$layout_file"; then
    echo "Extrude line missing in layout output" >&2
    exit 1
fi

# Force a layout with offset to test all sides
cat > "$layout_file" <<EOF
atlas 4,4
scale 1
extrude 1
sprite "$(fix_path "$frames_dir/red.png")" 1,1 2,2
EOF

sheet_file="$tmp_dir/sheet.png"
"$spratpack_bin" < "$layout_file" > "$sheet_file"

# Check pixels using python3 if available
if command -v python3 >/dev/null 2>&1; then
    python3 - <<EOF
import sys
from PIL import Image
img = Image.open("$(fix_path "$sheet_file")").convert("RGBA")
width, height = img.size
if width != 4 or height != 4:
    print(f"Invalid atlas size: {width}x{height}")
    sys.exit(1)

# Extruded pixels (the whole 4x4 should be red because of 1px extrusion around 2x2)
for y in range(0, 4):
    for x in range(0, 4):
        p = img.getpixel((x, y))
        if p != (255, 0, 0, 255):
            print(f"Pixel at {x},{y} is not red: {p}")
            sys.exit(1)
EOF
    echo "Pixel check passed"
fi

# Test CLI override
"$spratpack_bin" --extrude 0 < "$layout_file" > "$tmp_dir/no_extrude.png"
if command -v python3 >/dev/null 2>&1; then
    python3 - <<EOF
import sys
from PIL import Image
img = Image.open("$(fix_path "$tmp_dir/no_extrude.png")").convert("RGBA")
# Pixel 0,0 should be transparent (0,0,0,0)
p = img.getpixel((0, 0))
if p[3] != 0:
    print(f"Pixel at 0,0 should be transparent, got {p}")
    sys.exit(1)
EOF
    echo "CLI override check passed"
fi

# Test Rotated Extrusion
python3 -c "from PIL import Image; img = Image.new('RGBA', (1, 2)); img.putpixel((0,0), (255,0,0,255)); img.putpixel((0,1), (0,255,0,255)); img.save('$(fix_path "$frames_dir/rg.png")')"

cat > "$layout_file" <<EOF
atlas 4,3
scale 1
extrude 1
sprite "$(fix_path "$frames_dir/rg.png")" 1,1 2,1 rotated
EOF

"$spratpack_bin" < "$layout_file" > "$tmp_dir/rotated_extrude.png"

if command -v python3 >/dev/null 2>&1; then
    python3 - <<EOF
import sys
from PIL import Image
img = Image.open("$(fix_path "$tmp_dir/rotated_extrude.png")").convert("RGBA")
# Original 1x2 rotated 90 deg clockwise:
# col=0, row=0 -> sample_x=0+0=0, sample_y=0+(2-1-0)=1 -> Green
# col=1, row=0 -> sample_x=0+0=0, sample_y=0+(2-1-1)=0 -> Red
# So at (1,1) it is Green, at (2,1) it is Red.
p11 = img.getpixel((1, 1))
p21 = img.getpixel((2, 1))
if p11 != (0, 255, 0, 255) or p21 != (255, 0, 0, 255):
    print(f"Rotated pixels at 1,1 and 2,1 are wrong: {p11}, {p21}")
    sys.exit(1)

# Extrusion:
# Left of (1,1) (which is (0,1)) should be Green
# Right of (2,1) (which is (3,1)) should be Red
if img.getpixel((0, 1)) != (0, 255, 0, 255):
    print(f"Extruded left pixel is not Green: {img.getpixel((0, 1))}")
    sys.exit(1)
if img.getpixel((3, 1)) != (255, 0, 0, 255):
    print(f"Extruded right pixel is not Red: {img.getpixel((3, 1))}")
    sys.exit(1)
EOF
    echo "Rotated extrusion check passed"
fi

echo "extrude_test.sh: ok"
