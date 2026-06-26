#!/usr/bin/env bash
set -euo pipefail

if [ "${SPRAT_TEST_DEBUG:-0}" = "1" ]; then
    set -x
fi

if [ "$#" -ne 1 ]; then
    echo "Usage: incremental_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"

tmp_dir="$(mktemp -d)"
if [ "${SPRAT_TEST_DEBUG:-0}" = "1" ]; then
    echo "incremental_test tmp_dir: $tmp_dir" >&2
else
    trap 'rm -rf "$tmp_dir"' EXIT
fi

# Path conversion for Windows
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    tmp_dir_win="$(cygpath -m "$tmp_dir")"
    fix_path() {
        echo "${1/$tmp_dir/$tmp_dir_win}"
    }
else
    fix_path() {
        echo "$1"
    }
fi

frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"

# Isolate test from user configuration
mkdir -p "$tmp_dir/.config/sprat"
profiles_cfg="$tmp_dir/.config/sprat/spratprofiles.cfg"
cat > "$profiles_cfg" <<EOF
[profile fast]
mode=fast

[profile desktop]
mode=compact
optimize=gpu
EOF
export HOME="$tmp_dir"
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    export USERPROFILE="$(cygpath -w "$tmp_dir")"
fi

# Create a 1x1 PNG via base64
cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF

if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/pixel.b64" > "$frames_dir/a.png"
else
    base64 -D -i "$tmp_dir/pixel.b64" -o "$frames_dir/a.png"
fi
cp "$frames_dir/a.png" "$frames_dir/b.png"
cp "$frames_dir/a.png" "$frames_dir/c.png"

normalize_eol() {
    tr -d '\r' < "$1"
}

extract_sprite_positions() {
    grep '^sprite "' "$1" | sort
}

# --- Test 1: Initial incremental run produces valid output ---
layout1="$tmp_dir/layout1.txt"
"$spratlayout_bin" "$(fix_path "$frames_dir")" --incremental --mode compact --profiles-config "$(fix_path "$profiles_cfg")" > "$layout1"

sprite_count="$(grep -c '^sprite "' "$layout1" || true)"
if [ "$sprite_count" -ne 3 ]; then
    echo "Test 1 FAIL: Expected 3 sprites, got $sprite_count" >&2
    exit 1
fi

if ! grep -q '^atlas [0-9][0-9]*,[0-9][0-9]*$' "$layout1"; then
    echo "Test 1 FAIL: Missing or invalid atlas line" >&2
    exit 1
fi

# --- Test 2: Adding a sprite preserves existing positions ---
cp "$frames_dir/a.png" "$frames_dir/d.png"

layout2="$tmp_dir/layout2.txt"
"$spratlayout_bin" "$(fix_path "$frames_dir")" --incremental --mode compact --profiles-config "$(fix_path "$profiles_cfg")" > "$layout2"

sprite_count="$(grep -c '^sprite "' "$layout2" || true)"
if [ "$sprite_count" -ne 4 ]; then
    echo "Test 2 FAIL: Expected 4 sprites, got $sprite_count" >&2
    exit 1
fi

# Extract positions for a.png, b.png, c.png from both layouts and compare
for name in a.png b.png c.png; do
    pos1="$(grep "\"$name\"" "$layout1" || true)"
    pos2="$(grep "\"$name\"" "$layout2" || true)"
    if [ -z "$pos1" ] || [ -z "$pos2" ]; then
        echo "Test 2 FAIL: Sprite $name missing from one of the layouts" >&2
        exit 1
    fi
    if [ "$pos1" != "$pos2" ]; then
        echo "Test 2 FAIL: Position changed for $name" >&2
        echo "  layout1: $pos1" >&2
        echo "  layout2: $pos2" >&2
        exit 1
    fi
done

# Verify d.png is present
if ! grep -q '"d.png"' "$layout2"; then
    echo "Test 2 FAIL: New sprite d.png not present in layout2" >&2
    exit 1
fi

# --- Test 3: Removing a sprite preserves remaining positions ---
rm "$frames_dir/b.png"

layout3="$tmp_dir/layout3.txt"
"$spratlayout_bin" "$(fix_path "$frames_dir")" --incremental --mode compact --profiles-config "$(fix_path "$profiles_cfg")" > "$layout3"

sprite_count="$(grep -c '^sprite "' "$layout3" || true)"
if [ "$sprite_count" -ne 3 ]; then
    echo "Test 3 FAIL: Expected 3 sprites, got $sprite_count" >&2
    exit 1
fi

# Verify a.png, c.png, d.png keep their positions from layout2
for name in a.png c.png d.png; do
    pos2="$(grep "\"$name\"" "$layout2" || true)"
    pos3="$(grep "\"$name\"" "$layout3" || true)"
    if [ -z "$pos2" ] || [ -z "$pos3" ]; then
        echo "Test 3 FAIL: Sprite $name missing from one of the layouts" >&2
        exit 1
    fi
    if [ "$pos2" != "$pos3" ]; then
        echo "Test 3 FAIL: Position changed for $name after removal of b.png" >&2
        echo "  layout2: $pos2" >&2
        echo "  layout3: $pos3" >&2
        exit 1
    fi
done

# Verify b.png is absent
if grep -q '"b.png"' "$layout3"; then
    echo "Test 3 FAIL: Removed sprite b.png still present in layout3" >&2
    exit 1
fi

echo "All incremental packing tests passed."
