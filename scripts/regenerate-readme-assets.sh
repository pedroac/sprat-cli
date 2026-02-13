#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPRITELAYOUT_BIN="${SPRITELAYOUT_BIN:-$ROOT_DIR/spritelayout}"
SPRITEPACK_BIN="${SPRITEPACK_BIN:-$ROOT_DIR/spritepack}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/README-assets}"
FRAMES_DIR="${FRAMES_DIR:-$OUTPUT_DIR/frames}"
ROBOTFREE_ZIP_URL="${ROBOTFREE_ZIP_URL:-https://opengameart.org/sites/default/files/RobotFree.zip}"
FRAME_MAX_SIZE="${FRAME_MAX_SIZE:-64x64>}"

if [ ! -x "$SPRITELAYOUT_BIN" ]; then
    echo "Missing executable: $SPRITELAYOUT_BIN" >&2
    echo "Build first (for example: cmake --build .)" >&2
    exit 1
fi

if [ ! -x "$SPRITEPACK_BIN" ]; then
    echo "Missing executable: $SPRITEPACK_BIN" >&2
    echo "Build first (for example: cmake --build .)" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR" "$FRAMES_DIR"

if command -v magick >/dev/null 2>&1; then
    IM_CMD=(magick)
elif command -v convert >/dev/null 2>&1; then
    IM_CMD=(convert)
else
    echo "ImageMagick not found. Install 'magick' (preferred) or 'convert'." >&2
    exit 1
fi

if command -v curl >/dev/null 2>&1; then
    DOWNLOAD_WITH="curl"
elif command -v wget >/dev/null 2>&1; then
    DOWNLOAD_WITH="wget"
else
    echo "Downloader not found. Install 'curl' or 'wget'." >&2
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "Missing dependency: unzip" >&2
    exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

zip_path="$tmp_dir/RobotFree.zip"
extract_dir="$tmp_dir/extracted"
mkdir -p "$extract_dir"

echo "Downloading source zip..."
if [ "$DOWNLOAD_WITH" = "curl" ]; then
    curl -fL --retry 3 "$ROBOTFREE_ZIP_URL" -o "$zip_path"
else
    wget -O "$zip_path" "$ROBOTFREE_ZIP_URL"
fi

echo "Extracting source zip..."
unzip -q -o "$zip_path" -d "$extract_dir"

echo "Reducing source frames into $FRAMES_DIR..."
find "$FRAMES_DIR" -maxdepth 1 -type f -iname '*.png' -delete

frame_count=0
while IFS= read -r -d '' src_png; do
    out_png="$FRAMES_DIR/$(basename "$src_png")"
    "${IM_CMD[@]}" "$src_png" \
        -filter point \
        -resize "$FRAME_MAX_SIZE" \
        -strip \
        -define png:compression-level=9 \
        "$out_png"
    frame_count=$((frame_count + 1))
done < <(find "$extract_dir" -type f -iname '*.png' -print0 | sort -z)

if [ "$frame_count" -eq 0 ]; then
    echo "No PNG frames found in downloaded archive." >&2
    exit 1
fi

render_sample() {
    local output_name="$1"
    shift

    local layout_tmp
    local image_tmp
    layout_tmp="$(mktemp)"
    image_tmp="$(mktemp --suffix=.png)"

    "$SPRITELAYOUT_BIN" "$FRAMES_DIR" "$@" > "$layout_tmp"
    "$SPRITEPACK_BIN" < "$layout_tmp" > "$image_tmp"

    mv "$image_tmp" "$OUTPUT_DIR/$output_name"
    rm -f "$layout_tmp"
    echo "Wrote $OUTPUT_DIR/$output_name"
}

render_sample_with_frame_lines() {
    local output_name="$1"
    shift

    local layout_tmp
    local image_tmp
    layout_tmp="$(mktemp)"
    image_tmp="$(mktemp --suffix=.png)"

    "$SPRITELAYOUT_BIN" "$FRAMES_DIR" "$@" > "$layout_tmp"
    "$SPRITEPACK_BIN" --frame-lines < "$layout_tmp" > "$image_tmp"

    mv "$image_tmp" "$OUTPUT_DIR/$output_name"
    rm -f "$layout_tmp"
    echo "Wrote $OUTPUT_DIR/$output_name"
}

# Example recipes: profile recipes
render_sample "recipe-01-desktop.png" --profile desktop
render_sample "recipe-02-mobile.png" --profile mobile
render_sample "recipe-03-space.png" --profile space
render_sample "recipe-04-fast.png" --profile fast
render_sample "recipe-05-legacy.png" --profile legacy
render_sample "recipe-06-css.png" --profile css

# Example recipes: size/quality recipes
render_sample "recipe-07-trim-transparent.png" --profile desktop --trim-transparent
render_sample "recipe-08-padding-2.png" --profile desktop --padding 2
render_sample "recipe-09-max-1024.png" --profile desktop --max-width 1024 --max-height 1024
render_sample "recipe-10-mobile-tuned.png" --profile mobile --trim-transparent --padding 2 --max-width 2048 --max-height 2048

# Example recipes: rendering recipes with frame lines
render_sample_with_frame_lines "recipe-11-frame-lines-red.png" --profile desktop

layout_tmp="$(mktemp)"
image_tmp="$(mktemp --suffix=.png)"
"$SPRITELAYOUT_BIN" "$FRAMES_DIR" --profile desktop --trim-transparent --padding 2 > "$layout_tmp"
"$SPRITEPACK_BIN" --frame-lines --line-width 2 --line-color 0,255,0 < "$layout_tmp" > "$image_tmp"
mv "$image_tmp" "$OUTPUT_DIR/recipe-12-pipeline-lines-green.png"
rm -f "$layout_tmp"
echo "Wrote $OUTPUT_DIR/recipe-12-pipeline-lines-green.png"
