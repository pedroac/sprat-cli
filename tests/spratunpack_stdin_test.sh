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
if magick -version >/dev/null 2>&1; then
    magick -size 4x4 xc:transparent -fill red -draw "rectangle 1,1 2,2" "magic.png"
elif convert -version 2>&1 | grep -q ImageMagick; then
    convert -size 4x4 xc:transparent -fill red -draw "rectangle 1,1 2,2" "magic.png"
else
    png_b64_2='iVBORw0KGgoAAAANSUhEUgAAAAQAAAAEAgMAAADUn3btAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAJUExURQAAAP8AAP///2cZZB4AAAABdFJOUwBA5thmAAAAAWJLR0QCZgt8ZAAAAAd0SU1FB+oDBAIGN16qqSQAAAAldEVYdGRhdGU6Y3JlYXRlADIwMjYtMDMtMDRUMDI6MDY6NTUrMDA6MDB+kBKTAAAAJXRFWHRkYXRlOm1vZGlmeQAyMDI2LTAzLTA0VDAyOjA2OjU1KzAwOjAwD82qLwAAACh0RVh0ZGF0ZTp0aW1lc3RhbXAAMjAyNi0wMy0wNFQwMjowNjo1NSswMDowMFjYi/AAAAAOSURBVAjXY2BgEAFCBgAAqAApRmN0bwAAAABJRU5ErkJggg=='
    if base64 --decode >/dev/null 2>&1 <<<'AA=='; then
        printf '%s' "$png_b64_2" | base64 --decode > "magic.png"
    else
        printf '%s' "$png_b64_2" | base64 -D > "magic.png"
    fi
fi

"$spratframes_bin" magic.png | "$spratunpack_bin" > "$tar_out"
tar -tf "$tar_out" > "$tar_list"
grep -q 'sprite_0.png' "$tar_list"
rm magic.png
