#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: pipeline_test.sh <spratlayout-bin> <spratpack-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratpack_bin="$2"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"

cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF

base64 -d "$tmp_dir/pixel.b64" > "$frames_dir/frame_a.png"
cp "$frames_dir/frame_a.png" "$frames_dir/frame_b.png"

# Isolate test from user configuration and provide required profiles
mkdir -p "$tmp_dir/.config/sprat"
cat > "$tmp_dir/.config/sprat/spratprofiles.cfg" <<EOF
[profile fast]
mode=fast

[profile desktop]
mode=compact
optimize=gpu

[profile mobile]
mode=compact
optimize=gpu
max_width=2048
max_height=2048

[profile space]
mode=compact
optimize=space

[profile legacy]
mode=pot
max_width=1024
max_height=1024

[profile css]
mode=fast
EOF
export HOME="$tmp_dir"

layout_file="$tmp_dir/layout.txt"
default_layout_file="$tmp_dir/layout_default.txt"
fast_layout_file="$tmp_dir/layout_fast.txt"
css_layout_file="$tmp_dir/layout_css.txt"
sheet_file="$tmp_dir/spritesheet.png"

"$spratlayout_bin" "$frames_dir" --profile fast --padding 1 > "$layout_file"
"$spratlayout_bin" "$frames_dir" --padding 1 > "$default_layout_file"
"$spratlayout_bin" "$frames_dir" --profiles-config "$tmp_dir/missing.cfg" --padding 1 > "$default_layout_file.with_missing_cfg"
"$spratlayout_bin" "$frames_dir" --profile fast --padding 1 > "$fast_layout_file"
"$spratlayout_bin" "$frames_dir" --profile css --padding 1 > "$css_layout_file"

if ! cmp -s "$layout_file" "$default_layout_file"; then
    echo "Default profile output differs from --profile fast" >&2
    exit 1
fi

if ! cmp -s "$default_layout_file" "$default_layout_file.with_missing_cfg"; then
    echo "No-profile defaults should not depend on profile config path" >&2
    exit 1
fi

if ! grep -q '^atlas [0-9][0-9]*,[0-9][0-9]*$' "$layout_file"; then
    echo "Missing or invalid atlas line in layout output" >&2
    exit 1
fi

if ! grep -Eq '^scale 1(\.0+)?$' "$layout_file"; then
    echo "Missing or invalid default scale line in layout output" >&2
    exit 1
fi

if ! grep -q '^atlas [0-9][0-9]*,[0-9][0-9]*$' "$fast_layout_file"; then
    echo "Missing or invalid atlas line in fast layout output" >&2
    exit 1
fi

if ! grep -Eq '^scale 1(\.0+)?$' "$fast_layout_file"; then
    echo "Missing or invalid default scale line in fast layout output" >&2
    exit 1
fi

if ! grep -q '^atlas [0-9][0-9]*,[0-9][0-9]*$' "$css_layout_file"; then
    echo "Missing or invalid atlas line in css layout output" >&2
    exit 1
fi

if ! grep -Eq '^scale 1(\.0+)?$' "$css_layout_file"; then
    echo "Missing or invalid default scale line in css layout output" >&2
    exit 1
fi

sprite_count="$(grep -c '^sprite "' "$layout_file" || true)"
if [ "$sprite_count" -ne 2 ]; then
    echo "Expected 2 sprite lines, got $sprite_count" >&2
    exit 1
fi

atlas_dims="$(grep '^atlas ' "$layout_file" | head -n1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
read -r atlas_w atlas_h <<< "$atlas_dims"
max_xw="$(awk '/^sprite "/ { split($3, p, ","); split($4, s, ","); v = p[1] + s[1]; if (v > m) m = v } END { print m + 0 }' "$layout_file")"
max_yh="$(awk '/^sprite "/ { split($3, p, ","); split($4, s, ","); v = p[2] + s[2]; if (v > m) m = v } END { print m + 0 }' "$layout_file")"
if [ "$max_xw" -ne "$atlas_w" ] || [ "$max_yh" -ne "$atlas_h" ]; then
    echo "Atlas dimensions include trailing padding-only gap: atlas=${atlas_w}x${atlas_h} used=${max_xw}x${max_yh}" >&2
    exit 1
fi

# POT mode should avoid unnecessary square growth when a smaller POT rectangle fits.
pot_dir="$tmp_dir/pot_frames"
mkdir -p "$pot_dir"
for i in $(seq 1 17); do
    cp "$frames_dir/frame_a.png" "$pot_dir/frame_$i.png"
done

pot_layout="$tmp_dir/pot_layout.txt"
"$spratlayout_bin" "$pot_dir" --profile legacy > "$pot_layout"

pot_atlas="$(grep '^atlas ' "$pot_layout" | head -n 1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
read -r pot_w pot_h <<< "$pot_atlas"

if [ -z "${pot_w:-}" ] || [ -z "${pot_h:-}" ]; then
    echo "POT mode did not emit a valid atlas line" >&2
    exit 1
fi

if [ "$((pot_w & (pot_w - 1)))" -ne 0 ] || [ "$((pot_h & (pot_h - 1)))" -ne 0 ]; then
    echo "POT mode atlas dimensions are not powers of two: ${pot_w}x${pot_h}" >&2
    exit 1
fi

pot_area=$((pot_w * pot_h))
if [ "$pot_area" -gt 32 ]; then
    echo "POT mode wasted space for 17 pixels: atlas ${pot_w}x${pot_h} (area ${pot_area})" >&2
    exit 1
fi

pot_sprite_count="$(grep -c '^sprite "' "$pot_layout" || true)"
if [ "$pot_sprite_count" -ne 17 ]; then
    echo "Expected 17 sprite lines in POT test, got $pot_sprite_count" >&2
    exit 1
fi

# In compact mode, GPU optimization should not produce a worse max-side than space optimization.
compact_gpu_layout="$tmp_dir/compact_gpu_layout.txt"
compact_space_layout="$tmp_dir/compact_space_layout.txt"
"$spratlayout_bin" "$pot_dir" --profile desktop > "$compact_gpu_layout"
"$spratlayout_bin" "$pot_dir" --profile space > "$compact_space_layout"

gpu_dims="$(grep '^atlas ' "$compact_gpu_layout" | head -n 1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
space_dims="$(grep '^atlas ' "$compact_space_layout" | head -n 1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
read -r gpu_w gpu_h <<< "$gpu_dims"
read -r space_w space_h <<< "$space_dims"

gpu_max_side="$gpu_w"
if [ "$gpu_h" -gt "$gpu_max_side" ]; then
    gpu_max_side="$gpu_h"
fi
space_max_side="$space_w"
if [ "$space_h" -gt "$space_max_side" ]; then
    space_max_side="$space_h"
fi

if [ "$gpu_max_side" -gt "$space_max_side" ]; then
    echo "GPU optimization produced a worse max-side (${gpu_w}x${gpu_h}) than space optimization (${space_w}x${space_h})" >&2
    exit 1
fi

# Desktop profile (GPU-oriented) should not be worse than fast profile on max-side.
compact_fast_layout="$tmp_dir/compact_fast_layout.txt"
"$spratlayout_bin" "$pot_dir" --profile fast > "$compact_fast_layout"
fast_dims="$(grep '^atlas ' "$compact_fast_layout" | head -n 1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
read -r fast_w fast_h <<< "$fast_dims"
fast_max_side="$fast_w"
if [ "$fast_h" -gt "$fast_max_side" ]; then
    fast_max_side="$fast_h"
fi

if [ "$gpu_max_side" -gt "$fast_max_side" ]; then
    echo "Desktop profile max-side (${gpu_w}x${gpu_h}) is worse than fast profile (${fast_w}x${fast_h})" >&2
    exit 1
fi

# Respect explicit atlas limits in compact mode.
bounded_layout="$tmp_dir/compact_bounded_layout.txt"
"$spratlayout_bin" "$pot_dir" --profile desktop --max-height 4 > "$bounded_layout"
bounded_dims="$(grep '^atlas ' "$bounded_layout" | head -n 1 | sed -E 's/^atlas ([0-9]+),([0-9]+)$/\1 \2/')"
read -r bounded_w bounded_h <<< "$bounded_dims"
if [ "$bounded_h" -gt 4 ]; then
    echo "Compact mode exceeded max height limit: ${bounded_w}x${bounded_h}" >&2
    exit 1
fi

"$spratpack_bin" < "$layout_file" > "$sheet_file"

if [ ! -s "$sheet_file" ]; then
    echo "Spritesheet output is empty" >&2
    exit 1
fi

signature="$(head -c 8 "$sheet_file" | od -An -t x1 | tr -d ' \n')"
if [ "$signature" != "89504e470d0a1a0a" ]; then
    echo "Output is not a PNG file" >&2
    exit 1
fi

line_sheet="$tmp_dir/spritesheet_lines.png"
"$spratpack_bin" --frame-lines --line-width 1 --line-color 255,0,0 < "$layout_file" > "$line_sheet"
line_signature="$(head -c 8 "$line_sheet" | od -An -t x1 | tr -d ' \n')"
if [ "$line_signature" != "89504e470d0a1a0a" ]; then
    echo "Output with frame lines is not a PNG file" >&2
    exit 1
fi

# Create a padded image with transparent borders and verify trim output.
trim_dir="$tmp_dir/trim_frames"
mkdir -p "$trim_dir"
trim_source="$trim_dir/frame_trim.png"

cat > "$tmp_dir/seed_layout.txt" <<EOF
atlas 3,3
sprite "$frames_dir/frame_a.png" 1,1 1,1
EOF
"$spratpack_bin" < "$tmp_dir/seed_layout.txt" > "$trim_source"

trim_layout="$tmp_dir/trim_layout.txt"
trim_sheet="$tmp_dir/trim_sheet.png"
"$spratlayout_bin" "$trim_dir" --profile desktop --trim-transparent > "$trim_layout"

if ! grep -q '^atlas 1,1$' "$trim_layout"; then
    echo "Trim mode did not reduce padded image atlas to 1x1" >&2
    exit 1
fi

if ! grep -E -q '^sprite ".*frame_trim\.png" [0-9]+,[0-9]+ 1,1 1,1 1,1$' "$trim_layout"; then
    echo "Trim mode did not emit expected crop offsets" >&2
    exit 1
fi

"$spratpack_bin" < "$trim_layout" > "$trim_sheet"
trim_signature="$(head -c 8 "$trim_sheet" | od -An -t x1 | tr -d ' \n')"
if [ "$trim_signature" != "89504e470d0a1a0a" ]; then
    echo "Trim mode output is not a PNG file" >&2
    exit 1
fi

# Regression: cache must not freeze padding changes across trim toggles.
layout_trim_p2="$tmp_dir/layout_trim_p2.txt"
layout_notrim_p2="$tmp_dir/layout_notrim_p2.txt"
layout_notrim_p6="$tmp_dir/layout_notrim_p6.txt"
layout_trim_p2_again="$tmp_dir/layout_trim_p2_again.txt"
layout_trim_p6="$tmp_dir/layout_trim_p6.txt"
"$spratlayout_bin" "$frames_dir" --profile desktop --trim-transparent --padding 2 > "$layout_trim_p2"
"$spratlayout_bin" "$frames_dir" --profile desktop --padding 2 > "$layout_notrim_p2"
"$spratlayout_bin" "$frames_dir" --profile desktop --padding 6 > "$layout_notrim_p6"
"$spratlayout_bin" "$frames_dir" --profile desktop --trim-transparent --padding 2 > "$layout_trim_p2_again"
"$spratlayout_bin" "$frames_dir" --profile desktop --trim-transparent --padding 6 > "$layout_trim_p6"

atlas_notrim_p2="$(grep '^atlas ' "$layout_notrim_p2" | head -n1)"
atlas_notrim_p6="$(grep '^atlas ' "$layout_notrim_p6" | head -n1)"
atlas_trim_p2_again="$(grep '^atlas ' "$layout_trim_p2_again" | head -n1)"
atlas_trim_p6="$(grep '^atlas ' "$layout_trim_p6" | head -n1)"
if [ "$atlas_notrim_p2" = "$atlas_notrim_p6" ]; then
    echo "Padding change had no effect after trim toggle in non-trim mode" >&2
    exit 1
fi
if [ "$atlas_trim_p2_again" = "$atlas_trim_p6" ]; then
    echo "Padding change had no effect after trim toggle in trim mode" >&2
    exit 1
fi

# Verify explicit layout scale is emitted and spratpack honors scaled dimensions.
scaled_dir="$tmp_dir/scaled_frames"
mkdir -p "$scaled_dir"
scaled_source="$scaled_dir/frame_large.png"
cat > "$tmp_dir/seed_large_layout.txt" <<EOF
atlas 4,4
sprite "$frames_dir/frame_a.png" 0,0 1,1
EOF
"$spratpack_bin" < "$tmp_dir/seed_large_layout.txt" > "$scaled_source"

scaled_layout="$tmp_dir/scaled_layout.txt"
scaled_sheet="$tmp_dir/scaled_sheet.png"
"$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --scale 0.5 > "$scaled_layout"

if ! grep -Eq '^scale 0\.5[0-9]*$' "$scaled_layout"; then
    echo "Scaled layout did not emit expected scale line" >&2
    exit 1
fi

if ! grep -q '^atlas 2,2$' "$scaled_layout"; then
    echo "Scaled layout did not reduce atlas to 2x2" >&2
    exit 1
fi

"$spratpack_bin" < "$scaled_layout" > "$scaled_sheet"
scaled_signature="$(head -c 8 "$scaled_sheet" | od -An -t x1 | tr -d ' \n')"
if [ "$scaled_signature" != "89504e470d0a1a0a" ]; then
    echo "Scaled layout output is not a PNG file" >&2
    exit 1
fi

# Resolution mapping should combine with scale (effective = scale * target/source).
scaled_resolution_layout="$tmp_dir/scaled_resolution_layout.txt"
"$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 --target-resolution 2x2 > "$scaled_resolution_layout"

if ! grep -Eq '^scale 0\.5[0-9]*$' "$scaled_resolution_layout"; then
    echo "Resolution mapping did not emit expected scale line" >&2
    exit 1
fi

if ! grep -q '^atlas 2,2$' "$scaled_resolution_layout"; then
    echo "Resolution mapping did not reduce atlas to 2x2" >&2
    exit 1
fi

scaled_combined_layout="$tmp_dir/scaled_combined_layout.txt"
"$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 --target-resolution 2x2 --scale 0.5 > "$scaled_combined_layout"

if ! grep -Eq '^scale 0\.25[0-9]*$' "$scaled_combined_layout"; then
    echo "Combined source/target and scale did not emit expected scale line" >&2
    exit 1
fi

if ! grep -q '^atlas 1,1$' "$scaled_combined_layout"; then
    echo "Combined source/target and scale did not reduce atlas to 1x1" >&2
    exit 1
fi

# Validation: source/target must be provided together, format is WxH, and scale must be <= 1.
if "$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 > "$tmp_dir/invalid_source_only.out" 2>&1; then
    echo "Expected source-resolution without target-resolution to fail" >&2
    exit 1
fi

# Mismatched proportions should use selected reference axis for scaling.
scaled_largest_layout="$tmp_dir/scaled_largest_layout.txt"
"$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 --target-resolution 3x2 > "$scaled_largest_layout"
if ! grep -Eq '^scale 0\.75[0-9]*$' "$scaled_largest_layout"; then
    echo "Mismatched proportions did not use expected largest-ratio scale" >&2
    exit 1
fi

scaled_smallest_layout="$tmp_dir/scaled_smallest_layout.txt"
"$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 --target-resolution 3x2 --resolution-reference smallest > "$scaled_smallest_layout"
if ! grep -Eq '^scale 0\.5[0-9]*$' "$scaled_smallest_layout"; then
    echo "Mismatched proportions did not use expected smallest-ratio scale" >&2
    exit 1
fi

if "$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4X4 --target-resolution 2x2 > "$tmp_dir/invalid_resolution_format.out" 2>&1; then
    echo "Expected invalid resolution format (must be WxH with lowercase x) to fail" >&2
    exit 1
fi

if "$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --source-resolution 4x4 --target-resolution 3x2 --resolution-reference largest --resolution-reference smallest > "$tmp_dir/invalid_resolution_reference_repeat.out" 2>&1; then
    echo "Expected repeated --resolution-reference values to fail" >&2
    exit 1
fi

if "$spratlayout_bin" "$scaled_dir" --profile desktop --no-trim-transparent --scale 1.1 > "$tmp_dir/invalid_scale.out" 2>&1; then
    echo "Expected scale greater than 1 to fail" >&2
    exit 1
fi
