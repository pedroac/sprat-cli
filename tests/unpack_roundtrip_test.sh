#!/usr/bin/env bash
# spratunpack must give back the original sprites: rotation undone, trimmed pixels placed
# in their original canvas, from layouts and from every TexturePacker-style export.
# Malformed frames files must fail cleanly and write nothing.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: unpack_roundtrip_test.sh <spratpack-bin> <spratconvert-bin> <spratunpack-bin>" >&2
    exit 1
fi

pack_bin="$1"
convert_bin="$2"
unpack_bin="$3"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "unpack_roundtrip_test.sh: skipped (python with Pillow not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# a.png: 6x3 gradient, packed rotated. b.png: 8x8 with a 3x4 opaque block at (1,2), packed trimmed.
"$python_bin" - "$(fix_path "$tmp_dir")" <<'EOF'
import sys
from PIL import Image
root = sys.argv[1]
a = Image.new("RGBA", (6, 3))
for y in range(3):
    for x in range(6):
        a.putpixel((x, y), (x * 40, y * 80, 100, 255))
a.save(root + "/a.png")
b = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
for y in range(2, 6):
    for x in range(1, 4):
        b.putpixel((x, y), (255, x * 50, y * 30, 255))
b.save(root + "/b.png")
EOF

same_images() {
    "$python_bin" - "$(fix_path "$1")" "$(fix_path "$2")" <<'EOF'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGBA")
b = Image.open(sys.argv[2]).convert("RGBA")
if a.size != b.size or a.tobytes() != b.tobytes():
    sys.exit("FAILED: %s %s differs from %s %s" % (sys.argv[1], a.size, sys.argv[2], b.size))
EOF
}

image_size() {
    "$python_bin" -c "import sys; from PIL import Image; print('%dx%d' % Image.open(sys.argv[1]).size)" "$(fix_path "$1")"
}

layout="$tmp_dir/layout.txt"
{
    echo "root \"$(fix_path "$tmp_dir")\""
    echo "atlas 16,8"
    echo "sprite \"a.png\" 0,0 3,6 rotated"
    echo "sprite \"b.png\" 4,0 3,4 1,2 4,2"
} > "$layout"
"$pack_bin" < "$layout" > "$tmp_dir/atlas.png"

echo "Test 1: layout frames restore the original sprites"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$layout")" -o "$(fix_path "$tmp_dir/out_layout")" > /dev/null
same_images "$tmp_dir/out_layout/a.png" "$tmp_dir/a.png"
same_images "$tmp_dir/out_layout/b.png" "$tmp_dir/b.png"

echo "Test 2: TexturePacker-style exports restore the original sprites"
for t in phaser-hash phaser-array unity.json aseprite; do
    "$convert_bin" --transform "$t" -a atlas.png < "$layout" > "$tmp_dir/$t.json"
    "$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/$t.json")" -o "$(fix_path "$tmp_dir/out_$t")" > /dev/null
    same_images "$tmp_dir/out_$t/a.png" "$tmp_dir/a.png"
    same_images "$tmp_dir/out_$t/b.png" "$tmp_dir/b.png"
done

echo "Test 3: --keep-trim writes only the packed pixels"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$layout")" -o "$(fix_path "$tmp_dir/out_keep")" --keep-trim > /dev/null
[ "$(image_size "$tmp_dir/out_keep/b.png")" = "3x4" ]

echo "Test 4: scaled layouts and their exports restore the same scaled canvas"
sed 's/^atlas 16,8$/scale 0.5\natlas 16,8/' "$layout" > "$tmp_dir/scaled.txt"
"$convert_bin" --transform phaser-hash -a atlas.png < "$tmp_dir/scaled.txt" > "$tmp_dir/scaled.json"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/scaled.txt")" -o "$(fix_path "$tmp_dir/out_scaled_layout")" > /dev/null
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/scaled.json")" -o "$(fix_path "$tmp_dir/out_scaled_json")" > /dev/null
# 3x4 content with trims 1,2 4,2 scaled to 1,1 2,1 (rounded half away from zero)
[ "$(image_size "$tmp_dir/out_scaled_layout/b.png")" = "6x6" ]
same_images "$tmp_dir/out_scaled_layout/b.png" "$tmp_dir/out_scaled_json/b.png"

echo "Test 5: nine-patch frames drop the 1px marker border"
printf 'atlas 16,8\nsprite "n.9.png" 0,0 3,4 2,2 2,2\n' > "$tmp_dir/ninepatch.txt"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/ninepatch.txt")" -o "$(fix_path "$tmp_dir/out_nine")" > /dev/null
[ "$(image_size "$tmp_dir/out_nine/n.9.png")" = "5x6" ]

echo "Test 6: Phaser multi-atlas JSON unpacks the texture that matches the atlas"
printf 'atlas 16,8\nsprite "a.png" 0,0 3,6 rotated\nsprite "b.png" 4,0 3,4 1,2 4,2\natlas 4,4\nsprite "c.png" 0,0 2,2\n' \
    | "$convert_bin" --transform phaser-hash -a 'sheet%d.png' > "$tmp_dir/multi.json"
cp "$tmp_dir/atlas.png" "$tmp_dir/sheet0.png"
"$unpack_bin" "$(fix_path "$tmp_dir/sheet0.png")" -f "$(fix_path "$tmp_dir/multi.json")" -o "$(fix_path "$tmp_dir/out_multi")" > /dev/null
same_images "$tmp_dir/out_multi/b.png" "$tmp_dir/b.png"
[ ! -e "$tmp_dir/out_multi/c.png" ]
if "$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/multi.json")" -o "$(fix_path "$tmp_dir/out_nomatch")" > /dev/null 2>&1; then
    echo "FAILED: a multi-atlas JSON without a texture for the atlas must be refused" >&2
    exit 1
fi

echo "Test 7: bad frames fail in both output modes and write nothing"
printf '{"frames":{"ok":{"frame":{"x":0,"y":0,"w":2,"h":2}},"bad":{"frame":{"x":10,"y":0,"w":50,"h":50}}}}' > "$tmp_dir/oob.json"
printf '{"frames":{"huge":{"frame":{"x":2147483000,"y":0,"w":2000,"h":2}}}}' > "$tmp_dir/huge.json"
printf '{"frames":{"far":{"frame":{"x":0,"y":0,"w":2,"h":2},"spriteSourceSize":{"x":5,"y":0,"w":2,"h":2},"sourceSize":{"w":4,"h":2}}}}' > "$tmp_dir/canvas.json"
printf '{"frames":{"big":{"frame":{"x":1e300,"y":0,"w":2,"h":2}}}}' > "$tmp_dir/float.json"
for f in oob huge canvas float; do
    if "$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/$f.json")" -o "$(fix_path "$tmp_dir/out_bad_$f")" > /dev/null 2>&1; then
        echo "FAILED: $f.json must be refused" >&2
        exit 1
    fi
    if [ -e "$tmp_dir/out_bad_$f" ]; then
        echo "FAILED: $f.json left output behind" >&2
        exit 1
    fi
    if "$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/$f.json")" > "$tmp_dir/bad_$f.tar" 2> /dev/null; then
        echo "FAILED: $f.json must be refused in TAR output" >&2
        exit 1
    fi
done

echo "Test 8: output names get .png unless they already end with it; duplicates are refused"
printf '{"frames":{"walk.001":{"frame":{"x":0,"y":0,"w":2,"h":2}},"idle.PNG":{"frame":{"x":0,"y":0,"w":2,"h":2}}}}' > "$tmp_dir/names.json"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/names.json")" -o "$(fix_path "$tmp_dir/out_names")" > /dev/null
[ -f "$tmp_dir/out_names/walk.001.png" ] && [ -f "$tmp_dir/out_names/idle.PNG" ]
printf '{"frames":[{"filename":"a","frame":{"x":0,"y":0,"w":2,"h":2}},{"filename":"a.png","frame":{"x":0,"y":0,"w":2,"h":2}}]}' > "$tmp_dir/dupe.json"
if "$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/dupe.json")" -o "$(fix_path "$tmp_dir/out_dupe")" > /dev/null 2>&1; then
    echo "FAILED: frames writing the same file must be refused" >&2
    exit 1
fi

echo "Test 9: multipack layouts unpack the sprites of the given atlas, aliases included"
cp "$tmp_dir/a.png" "$tmp_dir/a_copy.png"
mp_layout="$tmp_dir/multipack.txt"
{
    echo "root \"$(fix_path "$tmp_dir")\""
    echo "multipack true"
    echo "atlas 16,8"
    echo "sprite \"a.png\" 0,0 3,6 rotated"
    echo "atlas 8,8"
    echo "sprite \"b.png\" 0,0 3,4 1,2 4,2"
    echo "alias \"a_copy.png\" \"a.png\""
} > "$mp_layout"
"$pack_bin" -a "$(fix_path "$tmp_dir/mp_%d.png")" < "$mp_layout"
# atlases have different sizes: the image size picks the atlas
"$unpack_bin" "$(fix_path "$tmp_dir/mp_0.png")" -f "$(fix_path "$mp_layout")" -o "$(fix_path "$tmp_dir/out_mp0")" > /dev/null
"$unpack_bin" "$(fix_path "$tmp_dir/mp_1.png")" -f "$(fix_path "$mp_layout")" -o "$(fix_path "$tmp_dir/out_mp1")" > /dev/null
same_images "$tmp_dir/out_mp0/a.png" "$tmp_dir/a.png"
same_images "$tmp_dir/out_mp0/a_copy.png" "$tmp_dir/a.png"
same_images "$tmp_dir/out_mp1/b.png" "$tmp_dir/b.png"
[ ! -e "$tmp_dir/out_mp0/b.png" ] && [ ! -e "$tmp_dir/out_mp1/a.png" ]
# same-size atlases are ambiguous without --atlas-index
sed 's/^atlas 16,8$/atlas 8,8/; s/^sprite "a.png" 0,0 3,6 rotated$/sprite "a.png" 0,0 3,6 rotated/' "$mp_layout" > "$tmp_dir/same_size.txt"
"$pack_bin" -a "$(fix_path "$tmp_dir/same_%d.png")" < "$tmp_dir/same_size.txt"
if "$unpack_bin" "$(fix_path "$tmp_dir/same_1.png")" -f "$(fix_path "$tmp_dir/same_size.txt")" -o "$(fix_path "$tmp_dir/out_ambiguous")" > /dev/null 2>&1; then
    echo "FAILED: same-size atlases must require --atlas-index" >&2
    exit 1
fi
"$unpack_bin" "$(fix_path "$tmp_dir/same_1.png")" -f "$(fix_path "$tmp_dir/same_size.txt")" --atlas-index 1 -o "$(fix_path "$tmp_dir/out_same1")" > /dev/null
same_images "$tmp_dir/out_same1/b.png" "$tmp_dir/b.png"
[ ! -e "$tmp_dir/out_same1/a.png" ]

echo "unpack_roundtrip_test.sh: ok"
