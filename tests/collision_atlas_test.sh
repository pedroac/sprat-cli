#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: collision_atlas_test.sh <spratconvert-bin>" >&2
    exit 1
fi

convert_bin="$1"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

layout_file="$tmp_dir/layout.txt"
cat > "$layout_file" <<'LAYOUT'
atlas 64,32
scale 1
sprite "./frames/a.png" 0,0 16,16
LAYOUT

secondary_args=(-a atlas_%d.png --normal-atlas normal_%d.png
                --collision-atlas collision_%d.png --collision-scale 0.25)

# ── json ─────────────────────────────────────────────────────────────────────
"$convert_bin" --transform json "${secondary_args[@]}" < "$layout_file" > "$tmp_dir/out.json"
grep -q '"has_collision_atlas": true' "$tmp_dir/out.json"
grep -q '"collision_atlas_path": "collision_0.png"' "$tmp_dir/out.json"
grep -q '"collision_atlas_scale": 0.25' "$tmp_dir/out.json"
grep -q '"collision_path": "collision_0.png"' "$tmp_dir/out.json"
grep -q '"has_normal_atlas": true' "$tmp_dir/out.json"
grep -q '"normal_path": "normal_0.png"' "$tmp_dir/out.json"

# ── godot ────────────────────────────────────────────────────────────────────
"$convert_bin" --transform godot "${secondary_args[@]}" < "$layout_file" > "$tmp_dir/out.godot"
grep -q '"collision_image": "collision_0.png"' "$tmp_dir/out.godot"
grep -q '"collision_scale": 0.25' "$tmp_dir/out.godot"
grep -q '"normal_image": "normal_0.png"' "$tmp_dir/out.godot"

# ── unity.json ───────────────────────────────────────────────────────────────
"$convert_bin" --transform unity.json "${secondary_args[@]}" < "$layout_file" > "$tmp_dir/out.unity.json"
grep -q '"name": "_CollisionMap"' "$tmp_dir/out.unity.json"
grep -q '"name": "_NormalMap"' "$tmp_dir/out.unity.json"

# ── absent by default ────────────────────────────────────────────────────────
"$convert_bin" --transform json < "$layout_file" > "$tmp_dir/plain.json"
grep -q '"has_collision_atlas": false' "$tmp_dir/plain.json"
if grep -q '"collision_atlas_path"' "$tmp_dir/plain.json"; then
    echo "collision_atlas_path must be omitted without --collision-atlas" >&2
    exit 1
fi
"$convert_bin" --transform godot < "$layout_file" > "$tmp_dir/plain.godot"
if grep -q 'collision' "$tmp_dir/plain.godot"; then
    echo "godot output must not mention collision without --collision-atlas" >&2
    exit 1
fi
"$convert_bin" --transform unity.json < "$layout_file" > "$tmp_dir/plain.unity.json"
if grep -q '_CollisionMap' "$tmp_dir/plain.unity.json"; then
    echo "unity.json output must not include _CollisionMap without --collision-atlas" >&2
    exit 1
fi

# ── invalid --collision-scale ────────────────────────────────────────────────
for bad in 0 -1 abc 1x; do
    if "$convert_bin" --collision-atlas c.png --collision-scale "$bad" \
            < "$layout_file" > /dev/null 2> "$tmp_dir/err.txt"; then
        echo "Expected failure for --collision-scale $bad" >&2
        exit 1
    fi
    grep -q 'Invalid collision scale' "$tmp_dir/err.txt"
done

echo "collision_atlas_test.sh: ok"
