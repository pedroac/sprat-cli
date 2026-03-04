#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: multipack_test.sh <spratlayout-bin> <spratpack-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratpack_bin="$2"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"

# Create a 1x1 pixel image
cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF

if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/pixel.b64" > "$frames_dir/f1.png"
else
    # macOS/BSD base64
    base64 -D -i "$tmp_dir/pixel.b64" -o "$frames_dir/f1.png"
fi
cp "$frames_dir/f1.png" "$frames_dir/f2.png"
cp "$frames_dir/f1.png" "$frames_dir/f3.png"

# We will limit atlas size to 1x1.
# Since each sprite is 1x1, they cannot fit 2 in one atlas because of padding 0 by default?
# Wait, spratlayout might pack 2 in 1x2 if I don't limit height.

layout_file="$tmp_dir/layout.txt"
"$spratlayout_bin" "$frames_dir" --max-width 1 --max-height 1 --multipack > "$layout_file"

atlas_count=$(grep -c "^atlas " "$layout_file")
if [ "$atlas_count" -ne 3 ]; then
    echo "Expected 3 atlases, got $atlas_count" >&2
    cat "$layout_file" >&2
    exit 1
fi

# Test TAR output
tar_file="$tmp_dir/atlases.tar"
"$spratpack_bin" < "$layout_file" > "$tar_file"

if ! tar -tf "$tar_file" | grep -q "atlas_0.png"; then
    echo "Missing atlas_0.png in TAR" >&2
    exit 1
fi
if ! tar -tf "$tar_file" | grep -q "atlas_1.png"; then
    echo "Missing atlas_1.png in TAR" >&2
    exit 1
fi
if ! tar -tf "$tar_file" | grep -q "atlas_2.png"; then
    echo "Missing atlas_2.png in TAR" >&2
    exit 1
fi

# Test picking a specific index
index_file="$tmp_dir/index_1.png"
"$spratpack_bin" --atlas-index 1 < "$layout_file" > "$index_file"

signature="$(head -c 8 "$index_file" | od -An -t x1 | tr -d ' 
')"
if [ "$signature" != "89504e470d0a1a0a" ]; then
    echo "Pick index output is not a PNG file" >&2
    exit 1
fi

echo "Multipack test passed!"
