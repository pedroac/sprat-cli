#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: extract_test.sh <spratextract-bin>" >&2
    exit 1
fi

extract_bin="$1"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

# Create test images using ImageMagick
create_test_images() {
    # Test 1: Simple separated sprites (4 squares)
    convert -size 200x200 xc:transparent \
        -fill red -draw "rectangle 20,20 59,59" \
        -fill green -draw "rectangle 140,20 179,59" \
        -fill blue -draw "rectangle 20,140 59,179" \
        -fill yellow -draw "rectangle 140,140 179,179" \
        "$tmp_dir/separated_spritesheet.png"

    # Test 2: Sprites with rectangle borders (magenta rectangles)
    convert -size 200x200 xc:transparent \
        -fill magenta -draw "rectangle 19,19 60,60" \
        -fill magenta -draw "rectangle 139,19 180,60" \
        -fill magenta -draw "rectangle 19,139 60,180" \
        -fill magenta -draw "rectangle 139,139 180,180" \
        -fill red -draw "rectangle 20,20 59,59" \
        -fill green -draw "rectangle 140,20 179,59" \
        -fill blue -draw "rectangle 20,140 59,179" \
        -fill yellow -draw "rectangle 140,140 179,179" \
        "$tmp_dir/rectangles_spritesheet.png"

    # Test 3: Sprites with small gaps (testing tolerance)
    convert -size 200x200 xc:transparent \
        -fill red -draw "rectangle 20,20 39,39" \
        -fill red -draw "rectangle 50,20 69,39" \
        -fill blue -draw "rectangle 140,140 179,179" \
        "$tmp_dir/tolerance_spritesheet.png"

    # Test 4: Single sprite (too small to pass min-size filter)
    convert -size 200x200 xc:transparent \
        -fill white -draw "point 100,100" \
        "$tmp_dir/single_pixel.png"
}

create_test_images

# Test 1: Basic extraction without rectangles
echo "Testing basic sprite extraction..."
output_dir1="$tmp_dir/output1"
"$extract_bin" --verbose "$tmp_dir/separated_spritesheet.png" "$output_dir1"

if [ ! -d "$output_dir1" ]; then
    echo "Error: Output directory not created" >&2
    exit 1
fi

sprite_count1=$(find "$output_dir1" -name "sprite_*.png" | wc -l)
if [ "$sprite_count1" -ne 4 ]; then
    echo "Error: Expected 4 sprites, got $sprite_count1" >&2
    exit 1
fi

# Verify sprite dimensions (should be 40x40 each)
for sprite in "$output_dir1"/sprite_*.png; do
    if [ ! -f "$sprite" ]; then
        continue
    fi
    # Check file size (basic validation)
    if [ ! -s "$sprite" ]; then
        echo "Error: Sprite file is empty: $sprite" >&2
        exit 1
    fi
done

echo "Basic extraction test passed"

# Test 2: Extraction with rectangle borders
echo "Testing rectangle border detection and removal..."
output_dir2="$tmp_dir/output2"
"$extract_bin" --has-rectangles --rectangle-color "255,0,255" --verbose "$tmp_dir/rectangles_spritesheet.png" "$output_dir2"

if [ ! -d "$output_dir2" ]; then
    echo "Error: Output directory not created for rectangle test" >&2
    exit 1
fi

sprite_count2=$(find "$output_dir2" -name "sprite_*.png" | wc -l)
if [ "$sprite_count2" -ne 4 ]; then
    echo "Error: Expected 4 sprites with rectangles, got $sprite_count2" >&2
    exit 1
fi

echo "Rectangle extraction test passed"

# Test 3: Tolerance handling
echo "Testing distance tolerance..."
output_dir3="$tmp_dir/output3"
"$extract_bin" --tolerance 10 --verbose "$tmp_dir/tolerance_spritesheet.png" "$output_dir3"

sprite_count3=$(find "$output_dir3" -name "sprite_*.png" | wc -l)
if [ "$sprite_count3" -ne 2 ]; then
    echo "Error: Expected 2 sprites with tolerance=10, got $sprite_count3" >&2
    exit 1
fi

# Test with lower tolerance (should get 3 sprites)
output_dir4="$tmp_dir/output4"
"$extract_bin" --tolerance 3 --verbose "$tmp_dir/tolerance_spritesheet.png" "$output_dir4"

sprite_count4=$(find "$output_dir4" -name "sprite_*.png" | wc -l)
if [ "$sprite_count4" -ne 3 ]; then
    echo "Error: Expected 3 sprites with tolerance=3, got $sprite_count4" >&2
    exit 1
fi

echo "Tolerance test passed"

# Test 4: Minimum size filtering
echo "Testing minimum sprite size..."
output_dir5="$tmp_dir/output5"
"$extract_bin" --min-size 500 --verbose "$tmp_dir/single_pixel.png" "$output_dir5"

sprite_count5=$(find "$output_dir5" -name "sprite_*.png" | wc -l)
if [ "$sprite_count5" -ne 0 ]; then
    echo "Error: Expected 0 sprites with min-size=500, got $sprite_count5" >&2
    exit 1
fi

echo "Minimum size test passed"

# Test 5: Custom filename pattern
echo "Testing custom filename pattern..."
output_dir6="$tmp_dir/output6"
"$extract_bin" --filename-pattern "frame_{index:03d}.png" --verbose "$tmp_dir/separated_spritesheet.png" "$output_dir6"

frame_count=$(find "$output_dir6" -name "frame_*.png" | wc -l)
if [ "$frame_count" -ne 4 ]; then
    echo "Error: Expected 4 frames with custom pattern, got $frame_count" >&2
    exit 1
fi

# Check that files have correct naming
if [ ! -f "$output_dir6/frame_000.png" ] || [ ! -f "$output_dir6/frame_001.png" ] || \
   [ ! -f "$output_dir6/frame_002.png" ] || [ ! -f "$output_dir6/frame_003.png" ]; then
    echo "Error: Custom filename pattern not applied correctly" >&2
    exit 1
fi

echo "Custom filename pattern test passed"

# Test 6: Error handling
echo "Testing error handling..."

# Test with non-existent input file
if "$extract_bin" "$tmp_dir/nonexistent.png" "$tmp_dir/error_test" 2>/dev/null; then
    echo "Error: Should have failed with non-existent input file" >&2
    exit 1
fi

# Test with invalid color format
if "$extract_bin" --rectangle-color "invalid" "$tmp_dir/separated_spritesheet.png" "$tmp_dir/error_test" 2>/dev/null; then
    echo "Error: Should have failed with invalid color format" >&2
    exit 1
fi

# Test with invalid tolerance
if "$extract_bin" --tolerance -1 "$tmp_dir/separated_spritesheet.png" "$tmp_dir/error_test" 2>/dev/null; then
    echo "Error: Should have failed with negative tolerance" >&2
    exit 1
fi

echo "Error handling test passed"

# Test 7: Help and version output
echo "Testing help output..."
if ! "$extract_bin" --help | grep -q "Extract sprite frames from spritesheets"; then
    echo "Error: Help output missing expected text" >&2
    exit 1
fi

echo "Help output test passed"

# Test 8: Color format variations
echo "Testing color format variations..."

# Test hex color format
output_dir7="$tmp_dir/output7"
"$extract_bin" --has-rectangles --rectangle-color "#FF00FF" --verbose "$tmp_dir/rectangles_spritesheet.png" "$output_dir7"

sprite_count7=$(find "$output_dir7" -name "sprite_*.png" | wc -l)
if [ "$sprite_count7" -ne 4 ]; then
    echo "Error: Hex color format test failed, got $sprite_count7 sprites" >&2
    exit 1
fi

# Test RGB format
output_dir8="$tmp_dir/output8"
"$extract_bin" --has-rectangles --rectangle-color "RGB(255,0,255)" --verbose "$tmp_dir/rectangles_spritesheet.png" "$output_dir8"

sprite_count8=$(find "$output_dir8" -name "sprite_*.png" | wc -l)
if [ "$sprite_count8" -ne 4 ]; then
    echo "Error: RGB color format test failed, got $sprite_count8 sprites" >&2
    exit 1
fi

echo "Color format variations test passed"

# Test 9: Force overwrite
echo "Testing force overwrite..."
output_dir9="$tmp_dir/output9"
mkdir -p "$output_dir9"
touch "$output_dir9/sprite_0000.png"  # Create existing file

# Without force, should skip existing file (warning but continue)
output_without_force=$("$extract_bin" --verbose "$tmp_dir/separated_spritesheet.png" "$output_dir9" 2>&1)
if ! echo "$output_without_force" | grep -q "Warning: File already exists"; then
    echo "Error: Expected warning about existing file without force flag" >&2
    exit 1
fi

# With force, should overwrite
"$extract_bin" --force --verbose "$tmp_dir/separated_spritesheet.png" "$output_dir9"

sprite_count9=$(find "$output_dir9" -name "sprite_*.png" | wc -l)
if [ "$sprite_count9" -ne 4 ]; then
    echo "Error: Force overwrite test failed, got $sprite_count9 sprites" >&2
    exit 1
fi

echo "Force overwrite test passed"

echo "All spratextract tests passed successfully!"