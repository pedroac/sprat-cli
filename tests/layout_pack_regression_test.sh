#!/usr/bin/env bash
# Layout and atlas output guarantees: pot atlases stay powers of two, scaled sprites keep a
# consistent original size, and spratpack's --dilate/--extrude behave as documented without
# touching neighbouring sprites.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: layout_pack_regression_test.sh <spratlayout-bin> <spratpack-bin> <spratconvert-bin>" >&2
    exit 1
fi

layout_bin="$1"
pack_bin="$2"
convert_bin="$3"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "layout_pack_regression_test.sh: skipped (python with Pillow not available)"
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
import os, random, sys
from PIL import Image
root = sys.argv[1]
random.seed(11)
os.makedirs(root + "/set")
for i in range(14):
    w, h = random.randint(6, 30), random.randint(6, 30)
    Image.new("RGBA", (w, h), (40 * (i % 6), 90, 200, 255)).save(f"{root}/set/s{i:02d}.png")
# a few small sprites whose total size is below any limit
os.makedirs(root + "/small")
for i in range(3):
    Image.new("RGBA", (10, 10), (200, 40 * i, 40, 255)).save(f"{root}/small/m{i}.png")
# 23x45 image whose opaque part makes separately rounded scaled margins drift (23x45 at 0.5)
os.makedirs(root + "/scaled")
im = Image.new("RGBA", (23, 45), (0, 0, 0, 0))
for y in range(1, 24):
    for x in range(2, 20):
        im.putpixel((x, y), (200, 100, 50, 255))
im.save(root + "/scaled/frame.png")
# packing sources for spratpack
os.makedirs(root + "/px")
Image.new("RGBA", (4, 4), (255, 0, 0, 255)).save(root + "/px/a.png")
Image.new("RGBA", (4, 4), (0, 0, 255, 255)).save(root + "/px/b.png")
im = Image.new("RGBA", (10, 4), (0, 0, 0, 0))
for y in range(4):
    for x in range(3):
        im.putpixel((x, y), (255, 0, 0, 255))
im.save(root + "/px/edge.png")
EOF

is_pot() { [ "$1" -gt 0 ] && [ $(( $1 & ($1 - 1) )) -eq 0 ]; }

echo "Test 1: pot atlases stay powers of two with padding, single and multipack"
for args in "--padding 2" "--padding 3 --multipack --max-width 96 --max-height 96" "--padding 1 --multipack --max-width 64 --max-height 64"; do
    # shellcheck disable=SC2086
    "$layout_bin" "$(fix_path "$tmp_dir/set")" --mode pot $args > "$tmp_dir/pot.txt"
    while read -r _ size; do
        w="${size%,*}"; h="${size#*,}"
        if ! is_pot "$w" || ! is_pot "$h"; then
            echo "FAILED: --mode pot $args produced a ${w}x${h} atlas" >&2
            exit 1
        fi
        if [ "$args" != "--padding 2" ] && { [ "$w" -gt 64 ] || [ "$h" -gt 64 ]; }; then
            echo "FAILED: --mode pot $args produced a ${w}x${h} atlas beyond the largest allowed power of two" >&2
            exit 1
        fi
    done < <(grep '^atlas ' "$tmp_dir/pot.txt")
done

echo "Test 1b: pot multipack fits a small sheet whatever the limit"
for limit in 32 48 128; do
    "$layout_bin" "$(fix_path "$tmp_dir/small")" --mode pot --multipack --max-width "$limit" --max-height "$limit" > "$tmp_dir/small.txt"
    [ "$(grep -c '^atlas ' "$tmp_dir/small.txt")" -eq 1 ]
    size="$(sed -n 's/^atlas //p' "$tmp_dir/small.txt")"
    is_pot "${size%,*}" && is_pot "${size#*,}"
done

echo "Test 2: pot refuses sprites larger than the largest power of two within the limits"
if "$layout_bin" "$(fix_path "$tmp_dir/set")" --mode pot --multipack --max-width 24 --max-height 24 > /dev/null 2> "$tmp_dir/pot.err"; then
    echo "FAILED: sprites up to 30px must not fit 16x16 pot atlases" >&2
    exit 1
fi
grep -q 'largest power of two' "$tmp_dir/pot.err"

echo "Test 3: a scaled, trimmed sprite keeps round(original size x scale)"
"$layout_bin" "$(fix_path "$tmp_dir/scaled")" --trim-transparent --scale 0.5 > "$tmp_dir/scaled.txt"
"$convert_bin" --transform phaser-hash < "$tmp_dir/scaled.txt" | tr -d ' \n' | grep -qF '"sourceSize":{"h":23,"w":12}'

echo "Test 4: --dilate N spreads colors N pixels and keeps them transparent"
printf 'atlas 10,4\nsprite "%s" 0,0 10,4\n' "$(fix_path "$tmp_dir/px/edge.png")" > "$tmp_dir/dilate.txt"
"$pack_bin" --dilate 3 < "$tmp_dir/dilate.txt" > "$tmp_dir/dilate.png"
"$python_bin" - "$(fix_path "$tmp_dir/dilate.png")" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
row = [im.getpixel((x, 0)) for x in range(3, 7)]
if row != [(255, 0, 0, 0)] * 3 + [(0, 0, 0, 0)]:
    sys.exit("FAILED: --dilate 3 gave %r" % row)
EOF

echo "Test 5: --extrude never overwrites a neighbouring sprite"
printf 'atlas 8,4\nsprite "%s" 0,0 4,4\nsprite "%s" 4,0 4,4\n' \
    "$(fix_path "$tmp_dir/px/a.png")" "$(fix_path "$tmp_dir/px/b.png")" > "$tmp_dir/extrude.txt"
"$pack_bin" --extrude 2 --dilate 2 < "$tmp_dir/extrude.txt" > "$tmp_dir/extrude.png"
"$python_bin" - "$(fix_path "$tmp_dir/extrude.png")" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
row = [im.getpixel((x, 1))[:3] for x in range(8)]
if row != [(255, 0, 0)] * 4 + [(0, 0, 255)] * 4:
    sys.exit("FAILED: neighbouring sprites were overwritten: %r" % row)
EOF

echo "layout_pack_regression_test.sh: ok"
