#!/bin/bash
set -e

# Configuration
ASSET_URL="https://opengameart.org/sites/default/files/RobotFree.zip"
ASSET_ZIP="RobotFree.zip"
ASSET_DIR="README-assets"
FRAMES_DIR="$ASSET_DIR/frames"
FRAME_MAX_SIZE="64x64>"

# Ensure directories exist
mkdir -p "$ASSET_DIR"
mkdir -p "$FRAMES_DIR"

# Download assets
if [ ! -f "$ASSET_DIR/$ASSET_ZIP" ]; then
    echo "Downloading assets..."
    curl -L -o "$ASSET_DIR/$ASSET_ZIP" "$ASSET_URL"
fi

# Extract and process frames
if [ -z "$(ls -A "$FRAMES_DIR")" ]; then
    echo "Extracting and processing frames..."
    unzip -j -o "$ASSET_DIR/$ASSET_ZIP" "png/*" -d "$FRAMES_DIR"
    
    # Resize frames to make spritesheets manageable for README
    # Requires ImageMagick
    mogrify -resize "$FRAME_MAX_SIZE" "$FRAMES_DIR"/*.png
fi

# Build paths
SPRATLAYOUT="./build/spratlayout"
SPRATPACK="./build/spratpack"
PROFILES_CONFIG="./spratprofiles.cfg"

if [ ! -f "$SPRATLAYOUT" ] || [ ! -f "$SPRATPACK" ]; then
    echo "Binaries not found. Please build sprat-cli first."
    exit 1
fi

echo "Generating recipes..."

# Recipe 1: Desktop (Default)
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile desktop > "$ASSET_DIR/layout_desktop.txt"
$SPRATPACK < "$ASSET_DIR/layout_desktop.txt" > "$ASSET_DIR/recipe-01-desktop.png"

# Recipe 2: Mobile
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile mobile > "$ASSET_DIR/layout_mobile.txt"
$SPRATPACK < "$ASSET_DIR/layout_mobile.txt" > "$ASSET_DIR/recipe-02-mobile.png"

# Recipe 3: Space (Optimized for space)
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile space > "$ASSET_DIR/layout_space.txt"
$SPRATPACK < "$ASSET_DIR/layout_space.txt" > "$ASSET_DIR/recipe-03-space.png"

# Recipe 4: Fast
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile fast > "$ASSET_DIR/layout_fast.txt"
$SPRATPACK < "$ASSET_DIR/layout_fast.txt" > "$ASSET_DIR/recipe-04-fast.png"

# Recipe 5: Legacy (POT)
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile legacy > "$ASSET_DIR/layout_legacy.txt"
$SPRATPACK < "$ASSET_DIR/layout_legacy.txt" > "$ASSET_DIR/recipe-05-legacy.png"

# Recipe 6: CSS
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile css > "$ASSET_DIR/layout_css.txt"
$SPRATPACK < "$ASSET_DIR/layout_css.txt" > "$ASSET_DIR/recipe-06-css.png"

# Recipe 7: Trim Transparent
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile desktop --trim-transparent > "$ASSET_DIR/layout_trim.txt"
$SPRATPACK < "$ASSET_DIR/layout_trim.txt" > "$ASSET_DIR/recipe-07-trim-transparent.png"

# Recipe 8: Padding
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile desktop --padding 2 > "$ASSET_DIR/layout_padding.txt"
$SPRATPACK < "$ASSET_DIR/layout_padding.txt" > "$ASSET_DIR/recipe-08-padding-2.png"

# Recipe 9: Max 800x600 (Hard limits)
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile desktop --max-width 800 --max-height 600 > "$ASSET_DIR/layout_max_800x600.txt"
$SPRATPACK < "$ASSET_DIR/layout_max_800x600.txt" > "$ASSET_DIR/recipe-09-max-800x600.png"

# Recipe 10: Frame Lines
$SPRATPACK --frame-lines --line-width 1 --line-color 255,0,0 < "$ASSET_DIR/layout_desktop.txt" > "$ASSET_DIR/recipe-10-frame-lines-red.png"

# Recipe 11: Pipeline with Lines
$SPRATLAYOUT "$FRAMES_DIR" --profiles-config "$PROFILES_CONFIG" --profile desktop --trim-transparent --padding 2 | \
  $SPRATPACK --frame-lines --line-width 2 --line-color 0,255,0 > "$ASSET_DIR/recipe-11-pipeline-lines-green.png"

echo "Done."