#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: convert_test.sh <spratconvert-bin>" >&2
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
sprite "./frames/b.png" 16,0 8,8 1,2 3,4
LAYOUT

"$convert_bin" --list-transforms > "$tmp_dir/list.txt"
for fmt in json csv xml css; do
  if ! grep -q "^${fmt}\b" "$tmp_dir/list.txt"; then
    echo "Missing transform in list: $fmt" >&2
    exit 1
  fi
done

"$convert_bin" --transform json < "$layout_file" > "$tmp_dir/out.json"
grep -q '"atlas": {"width": 64, "height": 32}' "$tmp_dir/out.json"
grep -q '"path": "./frames/b.png"' "$tmp_dir/out.json"

"$convert_bin" --transform csv < "$layout_file" > "$tmp_dir/out.csv"
grep -q '^index,path,x,y,w,h,src_x,src_y,trim_right,trim_bottom,has_trim$' "$tmp_dir/out.csv"
grep -q '^1,./frames/b.png,16,0,8,8,1,2,3,4,true$' "$tmp_dir/out.csv"

"$convert_bin" --transform xml < "$layout_file" > "$tmp_dir/out.xml"
grep -q '^<atlas width="64" height="32" scale="1">$' "$tmp_dir/out.xml"
grep -q 'has_trim="true"' "$tmp_dir/out.xml"

"$convert_bin" --transform css < "$layout_file" > "$tmp_dir/out.css"
grep -Fq '.sprite-1 {' "$tmp_dir/out.css"
grep -q '^  background-position: -16px -0px;$' "$tmp_dir/out.css"

custom_transform="$tmp_dir/custom.transform"
cat > "$custom_transform" <<'CUSTOM'
[meta]
name=custom

[header]
BEGIN {{atlas_width}}x{{atlas_height}} count={{sprite_count}}

[sprites]
{{index}}|{{path}}|{{x}},{{y}} {{w}}x{{h}}

[separator]
;

[footer]

END
CUSTOM

"$convert_bin" --transform "$custom_transform" < "$layout_file" > "$tmp_dir/out.custom"
grep -q '^BEGIN 64x32 count=2' "$tmp_dir/out.custom"
grep -q '1|./frames/b.png|16,0 8x8' "$tmp_dir/out.custom"

echo "convert_test.sh: ok"
