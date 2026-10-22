#!/usr/bin/env bash
# spratunpack must read TexturePacker-style JSON regardless of formatting, ignore the
# "meta" block, and refuse frame names that escape the output location.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratunpack_json_test.sh <spratunpack-bin>" >&2
    exit 1
fi

unpack_bin="$1"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "spratunpack_json_test.sh: skipped (python with Pillow not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# 20x10 atlas; the left 10x10 region has one distinct color per quadrant so the
# orientation of the extracted sprite can be checked.
"$python_bin" - "$(fix_path "$tmp_dir/atlas.png")" <<'EOF'
import sys
from PIL import Image
im = Image.new("RGBA", (20, 10), (0, 0, 0, 0))
colors = {(0, 0): (255, 0, 0, 255), (1, 0): (0, 255, 0, 255),
          (0, 1): (0, 0, 255, 255), (1, 1): (255, 255, 255, 255)}
for x in range(10):
    for y in range(10):
        im.putpixel((x, y), colors[(x // 5, y // 5)])
im.save(sys.argv[1])
EOF

pixel_at_origin() {
    "$python_bin" -c "import sys; from PIL import Image; print(Image.open(sys.argv[1]).convert('RGBA').getpixel((0, 0)))" "$(fix_path "$1")"
}

echo "Test 1: minified JSON with a rotated frame and a meta block"
cat > "$tmp_dir/min.json" <<'EOF'
{"frames":{"a":{"frame":{"x":0,"y":0,"w":10,"h":10},"rotated":true,"trimmed":false,"spriteSourceSize":{"x":0,"y":0,"w":10,"h":10},"sourceSize":{"w":10,"h":10}}},"meta":{"image":"atlas.png","size":{"w":20,"h":10}}}
EOF
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/min.json")" -o "$(fix_path "$tmp_dir/out_min")" > /dev/null 2> "$tmp_dir/err_min.txt"
if [ -s "$tmp_dir/err_min.txt" ]; then
    echo "FAILED: unexpected diagnostics:" >&2
    cat "$tmp_dir/err_min.txt" >&2
    exit 1
fi
if [ -e "$tmp_dir/out_min/meta.png" ]; then
    echo "FAILED: the meta block was extracted as a frame" >&2
    exit 1
fi
min_pixel="$(pixel_at_origin "$tmp_dir/out_min/a.png")"

echo "Test 2: pretty-printed JSON gives the same result"
"$python_bin" -c "import json, sys; print(json.dumps(json.load(open(sys.argv[1])), indent=2))" \
    "$(fix_path "$tmp_dir/min.json")" > "$tmp_dir/pretty.json"
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/pretty.json")" -o "$(fix_path "$tmp_dir/out_pretty")" > /dev/null
pretty_pixel="$(pixel_at_origin "$tmp_dir/out_pretty/a.png")"
if [ "$min_pixel" != "$pretty_pixel" ]; then
    echo "FAILED: minified ($min_pixel) and pretty ($pretty_pixel) JSON disagree on rotation" >&2
    exit 1
fi
if [ "$min_pixel" = "(255, 0, 0, 255)" ]; then
    echo "FAILED: rotated frame was extracted without undoing the rotation" >&2
    exit 1
fi

echo "Test 3: frame names that escape the output are refused in TAR output"
cat > "$tmp_dir/evil.json" <<'EOF'
{"frames": {"../evil": {"frame": {"x": 0, "y": 0, "w": 10, "h": 10}}}}
EOF
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/evil.json")" > "$tmp_dir/evil.tar" 2> /dev/null || true
if tar -tf "$tmp_dir/evil.tar" 2> /dev/null | grep -q '\.\./evil'; then
    echo "FAILED: TAR output contains an entry outside the archive root" >&2
    exit 1
fi

echo "Test 4: a huge original size from the frames file is refused, not allocated"
# 100000x100000 RGBA would be a 40 GB buffer; the 1x1 frame itself is fine.
cat > "$tmp_dir/huge.json" <<'EOF'
{"frames": {"big": {"frame": {"x": 0, "y": 0, "w": 1, "h": 1}, "trimmed": true,
  "spriteSourceSize": {"x": 0, "y": 0, "w": 1, "h": 1}, "sourceSize": {"w": 100000, "h": 100000}}}}
EOF
status=0
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/huge.json")" \
    -o "$(fix_path "$tmp_dir/out_huge")" > /dev/null 2> "$tmp_dir/err_huge.txt" || status=$?
if [ "$status" -ne 1 ] || ! grep -q 'larger than the supported' "$tmp_dir/err_huge.txt"; then
    cat "$tmp_dir/err_huge.txt" >&2
    echo "FAILED: a 100000x100000 original size must be refused with an error (exit $status)" >&2
    exit 1
fi
# --keep-trim writes only the packed pixels, so the same frame is fine there.
"$unpack_bin" "$(fix_path "$tmp_dir/atlas.png")" -f "$(fix_path "$tmp_dir/huge.json")" --keep-trim \
    -o "$(fix_path "$tmp_dir/out_huge_trim")" > /dev/null
[ -f "$tmp_dir/out_huge_trim/big.png" ]

echo "spratunpack_json_test.sh: ok"
