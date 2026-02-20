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
SPRATLAYOUT="./spratlayout"
SPRATPACK="./spratpack"
PROFILES_CONFIG="./spratprofiles.cfg"

if [ ! -f "$SPRATLAYOUT" ]; then
    SPRATLAYOUT="./build/spratlayout"
    SPRATPACK="./build/spratpack"
fi

if [ ! -f "$SPRATLAYOUT" ] || [ ! -f "$SPRATPACK" ]; then
    echo "Binaries not found. Please build sprat-cli first."
    exit 1
fi

echo "Generating recipes..."

# 1. Compact GPU
echo "Generating recipe: compact-gpu"
"$SPRATLAYOUT" "$FRAMES_DIR" --mode compact --optimize gpu --padding 2 > "$ASSET_DIR/compact_gpu_pad2.txt"
"$SPRATPACK" < "$ASSET_DIR/compact_gpu_pad2.txt" > "$ASSET_DIR/compact_gpu_pad2.png"

# 2. Compact Space
echo "Generating recipe: compact-space"
"$SPRATLAYOUT" "$FRAMES_DIR" --mode compact --optimize space --padding 2 > "$ASSET_DIR/compact_space_pad2.txt"
"$SPRATPACK" < "$ASSET_DIR/compact_space_pad2.txt" > "$ASSET_DIR/compact_space_pad2.png"

# 3. Fast
echo "Generating recipe: fast"
"$SPRATLAYOUT" "$FRAMES_DIR" --mode fast --padding 2 > "$ASSET_DIR/fast_pad2.txt"
"$SPRATPACK" < "$ASSET_DIR/fast_pad2.txt" > "$ASSET_DIR/fast_pad2.png"

# 4. POT
echo "Generating recipe: pot"
"$SPRATLAYOUT" "$FRAMES_DIR" --mode pot --padding 2 > "$ASSET_DIR/pot_pad2.txt"
"$SPRATPACK" < "$ASSET_DIR/pot_pad2.txt" > "$ASSET_DIR/pot_pad2.png"

# 5. Trim
echo "Generating recipe: trim"
"$SPRATLAYOUT" "$FRAMES_DIR" --trim-transparent --padding 2 > "$ASSET_DIR/trim_pad2_lines.txt"
"$SPRATPACK" --frame-lines --line-color 0,255,0 < "$ASSET_DIR/trim_pad2_lines.txt" > "$ASSET_DIR/trim_pad2_lines.png"

# 6. Resolution
echo "Generating recipe: resolution"
"$SPRATLAYOUT" "$FRAMES_DIR" --source-resolution 3840x2160 --target-resolution 1920x1080 --padding 2 > "$ASSET_DIR/res_3840x2160_1920x1080_pad2.txt"
"$SPRATPACK" < "$ASSET_DIR/res_3840x2160_1920x1080_pad2.txt" > "$ASSET_DIR/res_3840x2160_1920x1080_pad2.png"

echo "Done."