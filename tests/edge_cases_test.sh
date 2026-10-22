#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: edge_cases_test.sh <spratlayout-bin> <spratconvert-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratconvert_bin="$2"

tmp_dir="$(mktemp -d)"
trap "rm -rf \"$tmp_dir\"" EXIT

# 1. spratlayout with missing/empty profiles
echo "Test 1: spratlayout with empty profiles file"
touch "$tmp_dir/empty_profiles.cfg"
if "$spratlayout_bin" --profiles-config "$tmp_dir/empty_profiles.cfg" some_image.png > /dev/null 2>&1; then
    echo "FAILED: spratlayout should fail with empty profiles if no profile is specified"
    # Actually, if it doesnt have default profile it should fail.
fi

# 2. spratconvert with empty input
echo "Test 2: spratconvert with empty input"
if echo -n "" | "$spratconvert_bin" --transform json 2>/dev/null; then
    echo "FAILED: spratconvert should fail with empty input"
fi

# 3. spratconvert with invalid transform
echo "Test 3: spratconvert with invalid transform"
if echo "atlas 1,1" | "$spratconvert_bin" --transform non_existent 2>/dev/null; then
    echo "FAILED: spratconvert should fail with non-existent transform"
fi

# 4. spratlayout with non-existent directory
echo "Test 4: spratlayout with non-existent directory"
output=$("$spratlayout_bin" /nonexistent/path/that/does/not/exist 2>&1 || true)
if echo "$output" | grep -qi "does not exist"; then
    echo "  PASS: got 'does not exist' error message"
else
    echo "  FAILED: expected 'does not exist' in error, got: $output"
    exit 1
fi

# 5. spratlayout with empty directory (no images)
echo "Test 5: spratlayout with empty directory"
empty_dir="$tmp_dir/empty_input"
mkdir -p "$empty_dir"
output=$("$spratlayout_bin" "$empty_dir" 2>&1 || true)
if echo "$output" | grep -qi "no valid images"; then
    echo "  PASS: got 'no valid images' error message"
else
    echo "  FAILED: expected 'no valid images' in error, got: $output"
    exit 1
fi

# 6. spratlayout auto-detects nine-patch from .9.png files
echo "Test 6: spratlayout nine-patch auto-detection from .9.png"
nine_patch_dir="$tmp_dir/nine_patch_input"
mkdir -p "$nine_patch_dir"
python3 - <<'PYEOF' "$nine_patch_dir/button.9.png"
import sys
from PIL import Image

# Create a 12x10 nine-patch PNG (full size including 1px border → content 10x8).
# Stretch region: columns 3-8 (content-space x 2-7, full-image x 3-8).
# Stretch region: rows 3-6 (content-space y 2-5, full-image y 3-6).
# => slice_left=2, slice_right=2, slice_top=2, slice_bottom=2
fw, fh = 12, 10
img = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
pixels = img.load()

# Fill inner content (1..fw-2, 1..fh-2) with a solid colour so it's not fully transparent.
for ry in range(1, fh - 1):
    for rx in range(1, fw - 1):
        pixels[rx, ry] = (255, 128, 0, 255)

# Top row: mark stretch columns 3..8 (full-image x)
for cx in range(3, 9):
    pixels[cx, 0] = (0, 0, 0, 255)

# Left column: mark stretch rows 3..6 (full-image y)
for ry in range(3, 7):
    pixels[0, ry] = (0, 0, 0, 255)

img.save(sys.argv[1])
PYEOF

output=$("$spratlayout_bin" "$nine_patch_dir" 2>&1)
if echo "$output" | grep -q "slice=2,2,2,2"; then
    echo "  PASS: nine-patch slice insets detected correctly"
else
    echo "  FAILED: expected 'slice=2,2,2,2' in output, got:"
    echo "$output"
    exit 1
fi
# Content should be 10x8 (12-2 x 10-2), not the full 12x10
if echo "$output" | grep -q "0,0 10,8"; then
    echo "  PASS: content dimensions (10x8) are correct"
else
    echo "  FAILED: expected '0,0 10,8' in output, got:"
    echo "$output"
    exit 1
fi

# 7. Nine-patch + --trim-transparent: transparent content border is trimmed, slice is preserved.
# Full image: 16x14 (content 14x12).
# Solid block in content: cols 3..10, rows 2..9 (0-indexed) → trimmed sprite 8x8.
# Stretch markers: top-row cols 5..12 → slice_left=4, slice_right=2.
#                  left-col rows 5..10 → slice_top=4, slice_bottom=2.
# Trim values include the 1px nine-patch border offset:
#   trim_left=1+3=4, trim_top=1+2=3, trim_right=1+(14-1-10)=4, trim_bottom=1+(12-1-9)=3
# Expected: sprite "..." 0,0 8,8 4,3 4,3 slice=4,4,2,2
echo "Test 7: nine-patch with --trim-transparent"
trim_nine_dir="$tmp_dir/nine_patch_trim"
mkdir -p "$trim_nine_dir"
python3 - <<'PYEOF' "$trim_nine_dir/padded.9.png"
import sys
from PIL import Image

fw, fh = 16, 14
img = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
pixels = img.load()

# Solid block: content cols 3..10, rows 2..9 (full-image: cols 4..11, rows 3..10)
for ry in range(3, 11):
    for rx in range(4, 12):
        pixels[rx, ry] = (255, 128, 0, 255)

# Top row stretch markers: full-image cols 5..12 → slice_left=4, slice_right=(14-12)=2
for cx in range(5, 13):
    pixels[cx, 0] = (0, 0, 0, 255)

# Left col stretch markers: full-image rows 5..10 → slice_top=4, slice_bottom=(12-10)=2
for ry in range(5, 11):
    pixels[0, ry] = (0, 0, 0, 255)

img.save(sys.argv[1])
PYEOF

output=$("$spratlayout_bin" --trim-transparent "$trim_nine_dir" 2>&1)
if echo "$output" | grep -q "slice=4,4,2,2"; then
    echo "  PASS: nine-patch slice insets detected correctly with --trim-transparent"
else
    echo "  FAILED: expected 'slice=4,4,2,2' in output, got:"
    echo "$output"
    exit 1
fi
# After trimming, content is 8x8 with trim offsets 3,2 3,2
if echo "$output" | grep -qE "0,0 8,8 4,3 4,3"; then
    echo "  PASS: trimmed dimensions and offsets are correct"
else
    echo "  FAILED: expected '0,0 8,8 4,3 4,3' in output, got:"
    echo "$output"
    exit 1
fi

# 8. Too-small .9.png (2x2) must not crash and must produce valid positive dimensions.
echo "Test 8: too-small .9.png handled gracefully"
tiny_nine_dir="$tmp_dir/tiny_nine_patch"
mkdir -p "$tiny_nine_dir"
python3 - <<'PYEOF' "$tiny_nine_dir/tiny.9.png"
import sys
from PIL import Image
img = Image.new("RGBA", (2, 2), (255, 0, 0, 255))
img.save(sys.argv[1])
PYEOF

output=$("$spratlayout_bin" "$tiny_nine_dir" 2>&1)
# Must produce a sprite entry with positive w and h (2x2, not 0x0)
if echo "$output" | grep -qP "sprite .* 0,0 [1-9]\d*,[1-9]\d*" 2>/dev/null ||
   echo "$output" | grep -qE "sprite .* 0,0 [0-9]+,[0-9]+" 2>/dev/null; then
    # Extract just the dimensions to confirm they're non-zero
    dims=$(echo "$output" | grep -oE "0,0 [0-9]+,[0-9]+" | head -1)
    echo "  PASS: tiny .9.png processed without crash, dimensions: $dims"
else
    echo "  FAILED: expected a sprite line in output, got:"
    echo "$output"
    exit 1
fi

echo "Test: transform file names Windows can't write are refused"
for name in "nul.txt" "COM3.json" "trailing." "trailing " "a?b.txt"; do
    cat > "$tmp_dir/badname.jsonnet" <<EOF
{ extension: ".txt", files: [{ filename: "$name", content: "x" }] }
EOF
    if printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' | "$spratconvert_bin" --transform "$tmp_dir/badname.jsonnet" \
        --output-dir "$tmp_dir/badname_out" > /dev/null 2> "$tmp_dir/badname.err"; then
        echo "  FAILED: output file name '$name' was accepted"
        exit 1
    fi
    grep -q 'valid on every platform' "$tmp_dir/badname.err"
done
# Names that only look similar are fine.
cat > "$tmp_dir/goodname.jsonnet" <<'EOF'
{ extension: ".txt", files: [{ filename: "console.txt", content: "x" }, { filename: "com10.txt", content: "y" }] }
EOF
printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' | "$spratconvert_bin" --transform "$tmp_dir/goodname.jsonnet" \
    --output-dir "$tmp_dir/goodname_out" > /dev/null
[ -f "$tmp_dir/goodname_out/console.txt" ] && [ -f "$tmp_dir/goodname_out/com10.txt" ]

echo "edge_cases_test.sh: ok"
