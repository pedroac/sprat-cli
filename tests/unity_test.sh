#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: unity_test.sh <spratconvert-bin>" >&2
  exit 1
fi

convert_bin="$1"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

layout_file="$tmp_dir/layout.txt"
cat > "$layout_file" <<'LAYOUT'
atlas 100,200
scale 1
sprite "player.png" 10,20 30,40 5,5 5,5
LAYOUT

markers_file="$tmp_dir/markers.txt"
cat > "$markers_file" <<'MARKERS'
path "player.png"
- marker "pivot" point 20,30
MARKERS

custom_transform="$tmp_dir/test.jsonnet"
cat > "$custom_transform" <<'CUSTOM'
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";
local sprite_line(s) =
  "y=" + s.y + " unity_y=" + s.unity_y +
  " pxn=" + lib.format_double(s.pivot_x_norm) +
  " pyn=" + lib.format_double(s.pivot_y_norm) +
  " pynr=" + lib.format_double(s.pivot_y_norm_raw) +
  " nh=" + s.name_hash_decimal +
  " nhh=" + s.name_hash_hex;
{
  name: "test",
  extension: "",
  content: std.join("\n", [sprite_line(s) for s in sprat.sprites]) + "\n",
}
CUSTOM

"$convert_bin" --transform "$custom_transform" --markers "$markers_file" < "$layout_file" > "$tmp_dir/out.txt"

grep -q "y=20" "$tmp_dir/out.txt"
grep -q "unity_y=140" "$tmp_dir/out.txt"
grep -q "pxn=0.5" "$tmp_dir/out.txt"
grep -q "pyn=0.4" "$tmp_dir/out.txt"
grep -q "pynr=0.6" "$tmp_dir/out.txt"
grep -q "nh=" "$tmp_dir/out.txt"
grep -q "nhh=" "$tmp_dir/out.txt"

# unity.meta has no rotation field: rotated sprites are refused, and the unity group
# writes nothing at all when one of its members refuses the layout.
printf 'atlas 100,50\nsprite "rot.png" 0,0 30,50 rotated\n' > "$tmp_dir/rotated.txt"
if "$convert_bin" --transform unity.meta < "$tmp_dir/rotated.txt" > /dev/null 2>&1; then
  echo "unity.meta must refuse rotated sprites" >&2
  exit 1
fi
if "$convert_bin" --transform unity --output-dir "$tmp_dir/unity_rotated" < "$tmp_dir/rotated.txt" > /dev/null 2>&1; then
  echo "unity group must fail for rotated sprites" >&2
  exit 1
fi
if [ -e "$tmp_dir/unity_rotated" ]; then
  echo "unity group left partial output after a member failed" >&2
  exit 1
fi
printf 'atlas 100,50\nsprite "a.png" 0,0 30,50\n' | "$convert_bin" --transform unity --output-dir "$tmp_dir/unity_ok" > /dev/null
[ -f "$tmp_dir/unity_ok/meta.meta" ] && [ -f "$tmp_dir/unity_ok/json.json" ]

# unity.meta: the sprite rect is the trimmed content, so pivot and border are relative to it;
# IDs are signed 64-bit and spriteID a 32-hex GUID; names that YAML would misread are quoted.
printf 'atlas 64,64\nsprite "c.png" 0,0 10,10 10,10 10,10\n' > "$tmp_dir/trimmed.txt"
printf 'path "c.png"\n- marker "pivot" point 10,10\n' > "$tmp_dir/trimmed.markers"
"$convert_bin" --transform unity.meta --markers "$tmp_dir/trimmed.markers" < "$tmp_dir/trimmed.txt" > "$tmp_dir/trimmed.meta"
grep -qF 'pivot: {x: 0, y: 1}' "$tmp_dir/trimmed.meta"
grep -qE '^      spriteID: [0-9a-f]{32}$' "$tmp_dir/trimmed.meta"
internal_id="$(sed -n 's/^      internalID: //p' "$tmp_dir/trimmed.meta")"
grep -qF "213: $internal_id" "$tmp_dir/trimmed.meta"
# "c" hashes above INT64_MAX, so its signed ID must be negative
case "$internal_id" in
  -[1-9]*) ;;
  *) echo "internalID must be the signed 64-bit hash, got: $internal_id" >&2; exit 1 ;;
esac
dupes="$("$convert_bin" --transform unity.meta < "$tmp_dir/trimmed.txt" | grep -E '^  [A-Za-z]' | sort | uniq -d)"
if [ -n "$dupes" ]; then
  echo "unity.meta has duplicate TextureImporter keys: $dupes" >&2
  exit 1
fi
printf 'atlas 8,8\nsprite "a: b.png" 0,0 8,8\n' | "$convert_bin" --transform unity.meta | grep -qF 'name: "a: b"'
printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' > "$tmp_dir/anim.txt"
printf 'animation "#idle"\n- frame 0\n' > "$tmp_dir/anim.anims"
"$convert_bin" --transform unity.anim --animations "$tmp_dir/anim.anims" --output-dir "$tmp_dir/anim_out" < "$tmp_dir/anim.txt" > /dev/null
anim_meta="$(printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' | "$convert_bin" --transform unity.meta | sed -n 's/^      internalID: //p')"
grep -qF 'm_Name: "#idle"' "$tmp_dir/anim_out/"*.anim
grep -qF "value: {fileID: $anim_meta," "$tmp_dir/anim_out/"*.anim

# multipack: one <atlas>.meta per atlas with its own GUID; .anim frames reference the GUID of
# the atlas their sprite was packed into
printf 'atlas 32,32\nsprite "a.png" 0,0 16,16\natlas 32,32\nsprite "c.png" 0,0 8,8\n' > "$tmp_dir/mp.txt"
printf 'animation "walk"\n- frame 0\n- frame 1\n' > "$tmp_dir/mp.anims"
"$convert_bin" --transform unity --animations "$tmp_dir/mp.anims" --output-dir "$tmp_dir/mp_unity" < "$tmp_dir/mp.txt" > /dev/null
[ -f "$tmp_dir/mp_unity/atlas_0.png.meta" ] && [ -f "$tmp_dir/mp_unity/atlas_1.png.meta" ]
[ -f "$tmp_dir/mp_unity/atlas_0.json" ] && [ -f "$tmp_dir/mp_unity/atlas_1.json" ]
guid0="$(sed -n 's/^guid: //p' "$tmp_dir/mp_unity/atlas_0.png.meta")"
guid1="$(sed -n 's/^guid: //p' "$tmp_dir/mp_unity/atlas_1.png.meta")"
[ -n "$guid0" ] && [ -n "$guid1" ] && [ "$guid0" != "$guid1" ]
grep -qF '      name: c' "$tmp_dir/mp_unity/atlas_1.png.meta"
if grep -qF '      name: a' "$tmp_dir/mp_unity/atlas_1.png.meta"; then
  echo "atlas_1.png.meta must only list atlas 1 sprites" >&2
  exit 1
fi
id_c="$(sed -n 's/^      internalID: //p' "$tmp_dir/mp_unity/atlas_1.png.meta")"
grep -qF "fileID: $id_c, guid: $guid1, type: 3" "$tmp_dir/mp_unity/walk.anim"
grep -qF "guid: $guid0, type: 3" "$tmp_dir/mp_unity/walk.anim"

# unity.anim: file names drop path characters and a leading dot; m_Name keeps the real name
printf 'animation "../up"\n- frame 0\nanimation "sub/dir"\n- frame 0\n' > "$tmp_dir/paths.anims"
"$convert_bin" --transform unity.anim --animations "$tmp_dir/paths.anims" --output-dir "$tmp_dir/paths/out" < "$tmp_dir/anim.txt" > /dev/null
[ -f "$tmp_dir/paths/out/_._up.anim" ] && [ -f "$tmp_dir/paths/out/sub_dir.anim" ]
[ "$(find "$tmp_dir/paths" -type f | wc -l)" -eq 2 ]
grep -qF 'm_Name: ../up' "$tmp_dir/paths/out/_._up.anim"
grep -qF 'm_Name: sub/dir' "$tmp_dir/paths/out/sub_dir.anim"
