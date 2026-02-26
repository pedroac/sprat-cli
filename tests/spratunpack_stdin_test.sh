#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratunpack_stdin_test.sh <spratunpack-bin>" >&2
    exit 2
fi

spratunpack_bin="$1"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

atlas_png="$tmp_dir/atlas.png"
frames_file="$tmp_dir/frames.spratframes"
tar_out="$tmp_dir/out.tar"
tar_list="$tmp_dir/tar.list"

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
