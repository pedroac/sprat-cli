#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: layout_limits_and_multipack_optimize_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

fix_path() {
    local p="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$p"
    else
        printf '%s\n' "$p"
    fi
}

# Test 1: --no-max-width/--no-max-height must override profile limits.
frames_limits="$tmp_dir/frames_limits"
mkdir -p "$frames_limits"
cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF
if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/pixel.b64" > "$frames_limits/f1.png"
else
    base64 -D -i "$tmp_dir/pixel.b64" -o "$frames_limits/f1.png"
fi
cp "$frames_limits/f1.png" "$frames_limits/f2.png"

profiles_cfg="$tmp_dir/profiles.cfg"
cat > "$profiles_cfg" <<EOF
[profile constrained]
mode=compact
optimize=space
max_width=1
max_height=1
EOF

if "$spratlayout_bin" "$(fix_path "$frames_limits")" --profile constrained --profiles-config "$(fix_path "$profiles_cfg")" > "$tmp_dir/should_fail_layout.txt" 2> "$tmp_dir/should_fail.err"; then
    echo "Expected constrained profile run to fail without --no-max-* overrides" >&2
    exit 1
fi

"$spratlayout_bin" "$(fix_path "$frames_limits")" --profile constrained --profiles-config "$(fix_path "$profiles_cfg")" --no-max-width --no-max-height > "$tmp_dir/no_max_layout.txt"
if ! grep -q '^atlas [0-9][0-9]*,[0-9][0-9]*$' "$tmp_dir/no_max_layout.txt"; then
    echo "Expected a valid atlas line with --no-max-* overrides" >&2
    cat "$tmp_dir/no_max_layout.txt" >&2
    exit 1
fi

# Test 2: multipack should honor --optimize in candidate selection.
frames_multi="$tmp_dir/frames_multi"
mkdir -p "$frames_multi"
cat > "$tmp_dir/s10.b64" <<'EOF'
Qk12AQAAAAAAADYAAAAoAAAACgAAAAoAAAABABgAAAAAAEABAAATCwAAEwsAAAAAAAAAAAAAAAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAAAAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAAAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAAAAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAAAAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAAAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAAAAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAAAAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAP8AAAAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAA/wAAAAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAA=
EOF
cat > "$tmp_dir/s1x30.b64" <<'EOF'
Qk2uAAAAAAAAADYAAAAoAAAAAQAAAB4AAAABABgAAAAAAHgAAAATCwAAEwsAAAAAAAAAAAAAAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8A
EOF
if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/s10.b64" > "$frames_multi/a.bmp"
    base64 -d "$tmp_dir/s10.b64" > "$frames_multi/b.bmp"
    base64 -d "$tmp_dir/s1x30.b64" > "$frames_multi/c.bmp"
else
    base64 -D -i "$tmp_dir/s10.b64" -o "$frames_multi/a.bmp"
    base64 -D -i "$tmp_dir/s10.b64" -o "$frames_multi/b.bmp"
    base64 -D -i "$tmp_dir/s1x30.b64" -o "$frames_multi/c.bmp"
fi

"$spratlayout_bin" "$(fix_path "$frames_multi")" --mode compact --multipack --max-width 30 --max-height 30 --optimize gpu > "$tmp_dir/multipack_gpu.txt"
"$spratlayout_bin" "$(fix_path "$frames_multi")" --mode compact --multipack --max-width 30 --max-height 30 --optimize space > "$tmp_dir/multipack_space.txt"

gpu_atlas_count="$(grep -c '^atlas ' "$tmp_dir/multipack_gpu.txt" || true)"
space_atlas_count="$(grep -c '^atlas ' "$tmp_dir/multipack_space.txt" || true)"

if [ "$gpu_atlas_count" -le 0 ] || [ "$space_atlas_count" -le 0 ]; then
    echo "Expected at least one atlas for gpu and space multipack outputs" >&2
    exit 1
fi

sum_atlas_area() {
    local file="$1"
    awk '
        /^atlas / {
            split($2, dims, ",");
            w=dims[1]+0;
            h=dims[2]+0;
            total += (w*h);
        }
        END { print total + 0 }
    ' "$file"
}

gpu_total_area="$(sum_atlas_area "$tmp_dir/multipack_gpu.txt")"
space_total_area="$(sum_atlas_area "$tmp_dir/multipack_space.txt")"

if [ "$space_total_area" -gt "$gpu_total_area" ]; then
    echo "Expected optimize=space to have total atlas area <= optimize=gpu" >&2
    echo "gpu_total_area=$gpu_total_area space_total_area=$space_total_area" >&2
    echo "---gpu---" >&2
    cat "$tmp_dir/multipack_gpu.txt" >&2
    echo "---space---" >&2
    cat "$tmp_dir/multipack_space.txt" >&2
    exit 1
fi

echo "Layout limits + multipack optimize test passed!"
