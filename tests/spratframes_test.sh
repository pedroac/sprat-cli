#!/usr/bin/env bash
# spratframes must number frames in reading order, only treat a color as the background
# when all four corners share it, and only remove background pixels connected to the edge.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratframes_test.sh <spratframes-bin>" >&2
    exit 1
fi

frames_bin="$1"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "spratframes_test.sh: skipped (python with Pillow not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

"$python_bin" - "$(fix_path "$tmp_dir")" <<'EOF'
import sys
from PIL import Image, ImageDraw
root = sys.argv[1]

# Two rows of frames whose sizes grow and shrink, so size order and reading order differ.
im = Image.new("RGBA", (110, 40), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
for i, s in enumerate([6, 8, 10, 12]):
    d.rectangle([2 + i * 25, 2, 2 + i * 25 + s - 1, 2 + s - 1], fill=(200, 50, 50, 255))
for i, s in enumerate([12, 6]):
    d.rectangle([2 + i * 25, 22, 2 + i * 25 + s - 1, 22 + s - 1], fill=(50, 50, 200, 255))
im.save(root + "/rows.png")

# Tightly packed strip: the first frame covers the top-left corner.
im = Image.new("RGBA", (40, 10), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle([0, 0, 7, 7], fill=(200, 50, 50, 255))
d.rectangle([20, 0, 27, 7], fill=(200, 50, 50, 255))
im.save(root + "/packed.png")

# Solid magenta background; the sprite has a magenta stripe across its interior.
im = Image.new("RGBA", (24, 24), (255, 0, 255, 255))
d = ImageDraw.Draw(im)
d.rectangle([4, 4, 19, 19], fill=(255, 255, 0, 255), outline=(0, 0, 255, 255))
d.line([5, 11, 18, 11], fill=(255, 0, 255, 255), width=2)
im.save(root + "/magenta.png")
EOF

echo "Test 1: frames are numbered in reading order"
"$frames_bin" "$(fix_path "$tmp_dir/rows.png")" | grep '^sprite' > "$tmp_dir/rows.txt"
cat > "$tmp_dir/rows.expected" <<'EOF'
sprite 2,2 6,6
sprite 27,2 8,8
sprite 52,2 10,10
sprite 77,2 12,12
sprite 2,22 12,12
sprite 27,22 6,6
EOF
diff "$tmp_dir/rows.expected" "$tmp_dir/rows.txt"

echo "Test 2: a frame covering the top-left corner is not taken for the background"
"$frames_bin" "$(fix_path "$tmp_dir/packed.png")" > "$tmp_dir/packed.txt"
if grep -q '^background' "$tmp_dir/packed.txt"; then
    echo "FAILED: transparent corners must not produce a background" >&2
    exit 1
fi
[ "$(grep -c '^sprite' "$tmp_dir/packed.txt")" -eq 2 ]
grep -qx 'sprite 0,0 8,8' "$tmp_dir/packed.txt"

echo "Test 3: background-colored pixels inside a sprite are kept"
"$frames_bin" "$(fix_path "$tmp_dir/magenta.png")" > "$tmp_dir/magenta.txt"
grep -qx 'background ff00ff' "$tmp_dir/magenta.txt"
[ "$(grep -c '^sprite' "$tmp_dir/magenta.txt")" -eq 1 ]
grep -qx 'sprite 4,4 16,16' "$tmp_dir/magenta.txt"

echo "spratframes_test.sh: ok"
