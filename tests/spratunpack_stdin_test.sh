#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: spratunpack_stdin_test.sh <spratunpack-bin> [spratframes-bin]" >&2
    exit 2
fi

spratunpack_bin="$(realpath "$1")"
spratframes_bin="$(realpath "$2")"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

cd "$tmp_dir"

atlas_png="atlas.png"
frames_file="frames.spratframes"
tar_out="out.tar"
tar_list="tar.list"

cat > "$frames_file" <<'EOF'
sprite 0,0 1,1
EOF

png_b64='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO2WZ6kAAAAASUVORK5CYII='
if base64 --decode >/dev/null 2>&1 <<<'AA=='; then
    printf '%s' "$png_b64" | base64 --decode > "$atlas_png"
else
    printf '%s' "$png_b64" | base64 -D > "$atlas_png"
fi

cat "$atlas_png" | "$spratunpack_bin" -f "$frames_file" > "$tar_out"
tar -tf "$tar_out" > "$tar_list"
grep -qx 'sprite_0.png' "$tar_list"

cat "$atlas_png" | "$spratunpack_bin" - -f "$frames_file" > "$tar_out"
tar -tf "$tar_out" > "$tar_list"
grep -qx 'sprite_0.png' "$tar_list"

cat "$frames_file" | "$spratunpack_bin" "$atlas_png" -f - > "$tar_out"
tar -tf "$tar_out" > "$tar_list"
grep -qx 'sprite_0.png' "$tar_list"

# Test magic pipe: spratframes | spratunpack
# Create a proper image that spratframes can detect
if command -v convert >/dev/null 2>&1; then
    convert -size 4x4 xc:transparent -fill red -draw "rectangle 1,1 2,2" "magic.png"
elif command -v magick >/dev/null 2>&1; then
    magick -size 4x4 xc:transparent -fill red -draw "rectangle 1,1 2,2" "magic.png"
else
    png_b64_2='iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAB3RJTUUH6AMCFREpUunZFAAAAB1pVFh0Q29tbWVudAAAAAAAQ3JlYXRlZCB3aXRoIEdJTVBkLm3VAAAAF0lEQVQI12P8//8/AwMDA8P//xADDAwAFRMDf9m96ecAAAAASUVORK5CYII='
    if base64 --decode >/dev/null 2>&1 <<<'AA=='; then
        echo "$png_b64_2" | base64 --decode > "magic.png"
    else
        echo "$png_b64_2" | base64 -D > "magic.png"
    fi
fi

"$spratframes_bin" magic.png | "$spratunpack_bin" > "$tar_out"
tar -tf "$tar_out" > "$tar_list"
grep -q 'sprite_0.png' "$tar_list"
rm magic.png
