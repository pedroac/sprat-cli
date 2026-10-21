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

# 3x2 grid with 2px magenta lines shared between cells (12x12 interiors): one cell is empty,
# and one sprite contains a closed magenta loop of its own.
im = Image.new("RGBA", (3 * 14 + 2, 2 * 14 + 2), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
for c in range(4):
    d.rectangle([c * 14, 0, c * 14 + 1, 2 * 14 + 1], fill=(255, 0, 255, 255))
for r in range(3):
    d.rectangle([0, r * 14, 3 * 14 + 1, r * 14 + 1], fill=(255, 0, 255, 255))
for i, (c, r) in enumerate([(0, 0), (1, 0), (2, 0), (0, 1), (1, 1)]):
    d.rectangle([c * 14 + 4, r * 14 + 4, c * 14 + 9, r * 14 + 9], fill=(0, 40 * i + 40, 200, 255))
d.rectangle([14 + 5, 14 + 5, 14 + 8, 14 + 8], outline=(255, 0, 255, 255))
im.save(root + "/grid.png")

# A 6x4 sprite and a single stray pixel, for --tolerance.
im = Image.new("RGBA", (30, 12), (0, 0, 0, 0))
for y in range(3, 7):
    for x in range(3, 9):
        im.putpixel((x, y), (255, 0, 0, 255))
im.putpixel((20, 5), (255, 0, 0, 255))
im.save(root + "/tolerance.png")

# A tall frame at the top-left spanning two rows of small frames.
im = Image.new("RGBA", (80, 60), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle([2, 1, 9, 55], fill=(0, 0, 255, 255))
for y in (3, 33):
    for c in range(3):
        d.rectangle([20 + c * 20, y, 29 + c * 20, y + 19], fill=(255, 0, 0, 255))
im.save(root + "/tall.png")

# Two L-shaped sprites whose boxes clip each other, and a ring with a detached piece inside it.
im = Image.new("RGBA", (56, 32), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle([2, 2, 4, 20], fill=(255, 0, 0, 255))
d.rectangle([2, 18, 14, 20], fill=(255, 0, 0, 255))
d.rectangle([16, 10, 18, 28], fill=(0, 0, 255, 255))
d.rectangle([8, 10, 18, 12], fill=(0, 0, 255, 255))
d.rectangle([30, 2, 49, 21], outline=(0, 160, 0, 255))
d.rectangle([38, 10, 41, 13], fill=(0, 160, 0, 255))
im.save(root + "/merge.png")
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

echo "Test 4: --has-rectangles finds grid cells that share border lines"
"$frames_bin" --has-rectangles "$(fix_path "$tmp_dir/grid.png")" | grep '^sprite' > "$tmp_dir/grid.txt"
cat > "$tmp_dir/grid.expected" <<'EOF'
sprite 2,2 12,12
sprite 16,2 12,12
sprite 30,2 12,12
sprite 2,16 12,12
sprite 16,16 12,12
EOF
diff "$tmp_dir/grid.expected" "$tmp_dir/grid.txt"

echo "Test 5: --tolerance bridges gaps without growing frames or passing --min-size"
for t in 1 2 3; do
    "$frames_bin" --tolerance "$t" "$(fix_path "$tmp_dir/tolerance.png")" | grep '^sprite' > "$tmp_dir/tolerance_$t.txt"
    if [ "$(cat "$tmp_dir/tolerance_$t.txt")" != "sprite 3,3 6,4" ]; then
        echo "FAILED: --tolerance $t gave:" >&2
        cat "$tmp_dir/tolerance_$t.txt" >&2
        exit 1
    fi
done

echo "Test 6: a tall frame does not swallow the rows below it"
"$frames_bin" "$(fix_path "$tmp_dir/tall.png")" | grep '^sprite' > "$tmp_dir/tall.txt"
cat > "$tmp_dir/tall.expected" <<'EOF'
sprite 2,1 8,55
sprite 20,3 10,20
sprite 40,3 10,20
sprite 60,3 10,20
sprite 20,33 10,20
sprite 40,33 10,20
sprite 60,33 10,20
EOF
diff "$tmp_dir/tall.expected" "$tmp_dir/tall.txt"

echo "Test 7: boxes merge only when one mostly covers the other"
"$frames_bin" "$(fix_path "$tmp_dir/merge.png")" | grep '^sprite' > "$tmp_dir/merge.txt"
cat > "$tmp_dir/merge.expected" <<'EOF'
sprite 2,2 13,19
sprite 8,10 11,19
sprite 30,2 20,20
EOF
diff "$tmp_dir/merge.expected" "$tmp_dir/merge.txt"

echo "spratframes_test.sh: ok"
