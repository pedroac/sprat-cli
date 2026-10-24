#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: convert_test.sh <spratconvert-bin>" >&2
  exit 1
fi

convert_bin="$1"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

# Path conversion for Windows - Cache the base temp dir once
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

layout_file="$tmp_dir/layout.txt"
cat > "$layout_file" <<'LAYOUT'
atlas 64,32
scale 1
sprite "./frames/a.png" 0,0 16,16
sprite "./frames/b.png" 16,0 8,8 1,2 3,4 rotated
LAYOUT

layout_quotes_file="$tmp_dir/layout.quotes.txt"
cat > "$layout_quotes_file" <<'LAYOUTQ'
atlas 8,8
scale 1
sprite "./frames/a\"q.png" 0,0 8,8
LAYOUTQ

"$convert_bin" --list-transforms > "$tmp_dir/list.txt"
for fmt in JSON CSV XML CSS; do
  if ! grep -qi "^${fmt}\b" "$tmp_dir/list.txt"; then
    echo "Missing transform in list: $fmt" >&2
    exit 1
  fi
done

"$convert_bin" --transform json < "$layout_file" > "$tmp_dir/out.json"
grep -q '"width": 64' "$tmp_dir/out.json"
grep -q '"height": 32' "$tmp_dir/out.json"
grep -q '"multipack": false' "$tmp_dir/out.json"
grep -q '"extrude": 0' "$tmp_dir/out.json"
grep -q '"path": "./frames/b.png"' "$tmp_dir/out.json"
grep -q '"atlas_index": 0' "$tmp_dir/out.json"

"$convert_bin" --transform csv < "$layout_file" > "$tmp_dir/out.csv"
grep -q '^index,name,path,atlas_index,atlas_path,x,y,w,h,pivot_x,pivot_y,trim_left,trim_top,trim_right,trim_bottom,marker_count,markers_json,rotation,has_slice,slice_left,slice_top,slice_right,slice_bottom,slice_h,slice_v$' "$tmp_dir/out.csv"
grep -q '^1,b,./frames/b.png,0,,16,0,8,8,0,0,1,2,3,4,0,\[\],90,false,,,,,,$' "$tmp_dir/out.csv"

"$convert_bin" --transform xml < "$layout_file" > "$tmp_dir/out.xml"
grep -q '<layout multipack="false" scale="1" extrude="0">$' "$tmp_dir/out.xml"
grep -q '<atlas index="0" width="64" height="32" path="">' "$tmp_dir/out.xml"
grep -q 'trim_left="1" trim_top="2" trim_right="3" trim_bottom="4"' "$tmp_dir/out.xml"

"$convert_bin" --transform css < "$layout_file" > "$tmp_dir/out.css"
grep -Fq '.sprite-b {' "$tmp_dir/out.css"
grep -q '^  background-position: -16px -0px;$' "$tmp_dir/out.css"

# ── custom transform (Jsonnet) ───────────────────────────────────────────────
custom_transform="$tmp_dir/custom.jsonnet"
cat > "$custom_transform" <<'CUSTOM'
local sprat = std.extVar("sprat");
local sprite_line(s) =
  "" + s.index + "|" + s.path + "|" + s.x + "," + s.y + " " + s.w + "x" + s.h + " rotated=" + s.rotated;
{
  name: "custom",
  extension: "",
  content:
    "BEGIN " + sprat.atlas_width + "x" + sprat.atlas_height + " count=" + sprat.sprite_count + "\n" +
    std.join("\n;\n", [sprite_line(s) for s in sprat.sprites]) + "\n\nEND\n",
}
CUSTOM

"$convert_bin" --transform "$(fix_path "$custom_transform")" < "$layout_file" > "$tmp_dir/out.custom"
grep -q '^BEGIN 64x32 count=2' "$tmp_dir/out.custom"
grep -q '0|./frames/a.png|0,0 16x16 rotated=false' "$tmp_dir/out.custom"
grep -q '1|./frames/b.png|16,0 8x8 rotated=true' "$tmp_dir/out.custom"

# ── source_size transform (Jsonnet) ─────────────────────────────────────────
source_size_transform="$tmp_dir/source_size.jsonnet"
cat > "$source_size_transform" <<'SRCSIZE'
local sprat = std.extVar("sprat");
local sprite_line(s) = "" + s.index + "|" + s.source_w + "x" + s.source_h + "|" + s.has_trim;
{
  name: "source_size",
  extension: "",
  content: std.join("\n", [sprite_line(s) for s in sprat.sprites]) + "\n",
}
SRCSIZE

"$convert_bin" --transform "$(fix_path "$source_size_transform")" < "$layout_file" > "$tmp_dir/out.source_size"
# sprite a: no trim, source size equals packed size (16x16)
grep -q '0|16x16|false' "$tmp_dir/out.source_size"
# sprite b: trim_left=1 trim_top=2 trim_right=3 trim_bottom=4, packed 8x8 => source 12x14
grep -q '1|12x14|true' "$tmp_dir/out.source_size"

markers_file="$tmp_dir/markers.txt"
cat > "$markers_file" <<'MARKERS'
path "./frames/a.png"
- marker "hit" point 3,5
- marker "hurt" circle 6,7 4
path "b"
- marker "foot" rectangle 1,2 3,4
MARKERS

animations_file="$tmp_dir/animations.txt"
cat > "$animations_file" <<'ANIMS'
animation "run"
- frame "./frames/a.png"
- frame "b"
animation "idle"
- frame 1
ANIMS

# ── extras transform (Jsonnet) ───────────────────────────────────────────────
extras_transform="$tmp_dir/extras.jsonnet"
cat > "$extras_transform" <<'EXTRAS'
local sprat = std.extVar("sprat");

local fmt_marker(m) =
  '{"name":' + std.manifestJson(m.name) + ',"type":' + std.manifestJson(m.type) +
  ',"x":' + m.x + ',"y":' + m.y +
  (if m.type == "circle" then ',"radius":' + m.radius else "") +
  (if m.type == "rectangle" then ',"w":' + m.w + ',"h":' + m.h else "") +
  (if m.type == "polygon" then
    ',"vertices":[' + std.join(",", ['{"x":' + v.x + ',"y":' + v.y + '}' for v in m.vertices]) + ']'
   else "") +
  "}";

local fmt_markers_json(markers) = "[" + std.join(",", [fmt_marker(m) for m in markers]) + "]";

local sprite_line(s) =
  "" + s.index + "|" + s.name + "|" + s.path + "|" +
  std.length(s.markers) + "|" + fmt_markers_json(s.markers);

local header =
  "markers=" + sprat.has_markers + " animations=" + sprat.has_animations + "\n" +
  "markers_path=" + sprat.markers_path + "\n" +
  "animations_path=" + sprat.animations_path + "\n" +
  "marker_count=" + sprat.marker_count + "\n" +
  "animation_count=" + sprat.animation_count + "\n\n";

{
  name: "extras",
  extension: "",
  content: header + std.join("", [sprite_line(s) + "\n" for s in sprat.sprites]),
}
EXTRAS

"$convert_bin" --transform "$(fix_path "$extras_transform")" --markers "$(fix_path "$markers_file")" --animations "$(fix_path "$animations_file")" < "$layout_file" > "$tmp_dir/out.extras"
grep -q '^markers=true animations=true$' "$tmp_dir/out.extras"
grep -q "^markers_path=$(fix_path "$markers_file")$" "$tmp_dir/out.extras"
grep -q "^animations_path=$(fix_path "$animations_file")$" "$tmp_dir/out.extras"
grep -q '^marker_count=3$' "$tmp_dir/out.extras"
grep -q '^animation_count=2$' "$tmp_dir/out.extras"
grep -Fq '0|a|./frames/a.png|2|[{"name":"hit","type":"point","x":3,"y":5},{"name":"hurt","type":"circle","x":6,"y":7,"radius":4}]' "$tmp_dir/out.extras"
grep -Fq '1|b|./frames/b.png|1|[{"name":"foot","type":"rectangle","x":1,"y":2,"w":3,"h":4}]' "$tmp_dir/out.extras"

# ── iter transform (Jsonnet) ─────────────────────────────────────────────────
iter_transform="$tmp_dir/iter.jsonnet"
cat > "$iter_transform" <<'ITER'
local sprat = std.extVar("sprat");

local markers_content =
  if sprat.has_markers then
    "M_ON\n\nM_BEGIN\n\n" +
    std.join("|", [
      "M" + m.index + "=" + m.name + "@" + m.sprite_index + ":" + m.sprite_name + "\n"
      for m in sprat.markers
    ]) +
    "M_END\n\n"
  else
    "M_EMPTY\n\n";

local sprites_content =
  std.join("\n;\n", ["S" + s.index + "=" + s.path for s in sprat.sprites]) + "\n\n";

local anims_content =
  if sprat.has_animations then
    "A_ON\n\nA_BEGIN\n\n" +
    std.join("|", [
      "A" + a.index + "=" + a.name + ":[" +
      std.join(",", ["" + idx for idx in a.frame_indices]) + "]\n"
      for a in sprat.animations
    ]) +
    "A_END\n\n"
  else
    "A_EMPTY\n\n";

{
  name: "iter",
  extension: "",
  content: "BEGIN\n\n" + markers_content + sprites_content + anims_content + "END\n",
}
ITER

"$convert_bin" --transform "$(fix_path "$iter_transform")" --markers "$(fix_path "$markers_file")" --animations "$(fix_path "$animations_file")" < "$layout_file" > "$tmp_dir/out.iter.full"
grep -q '^M_ON$' "$tmp_dir/out.iter.full"
grep -q '^M_BEGIN$' "$tmp_dir/out.iter.full"
grep -q '^M0=hit@0:a$' "$tmp_dir/out.iter.full"
grep -q '^\|M1=hurt@0:a$' "$tmp_dir/out.iter.full"
grep -q '^\|M2=foot@1:b$' "$tmp_dir/out.iter.full"
grep -q '^A_ON$' "$tmp_dir/out.iter.full"
grep -q '^A_BEGIN$' "$tmp_dir/out.iter.full"
grep -q '^A0=run:\[0,1\]$' "$tmp_dir/out.iter.full"
grep -q '^\|A1=idle:\[1\]$' "$tmp_dir/out.iter.full"
if grep -q '^M_EMPTY$' "$tmp_dir/out.iter.full"; then
  echo "unexpected marker-empty branch in full iteration output" >&2
  exit 1
fi
if grep -q '^A_EMPTY$' "$tmp_dir/out.iter.full"; then
  echo "unexpected animation-empty branch in full iteration output" >&2
  exit 1
fi

"$convert_bin" --transform "$(fix_path "$iter_transform")" < "$layout_file" > "$tmp_dir/out.iter.empty"
grep -q '^M_EMPTY$' "$tmp_dir/out.iter.empty"
grep -q '^A_EMPTY$' "$tmp_dir/out.iter.empty"
if grep -q '^M_ON$' "$tmp_dir/out.iter.empty"; then
  echo "unexpected marker-present branch in empty iteration output" >&2
  exit 1
fi
if grep -q '^A_ON$' "$tmp_dir/out.iter.empty"; then
  echo "unexpected animation-present branch in empty iteration output" >&2
  exit 1
fi

"$convert_bin" --transform json --markers "$(fix_path "$markers_file")" --animations "$(fix_path "$animations_file")" < "$layout_file" > "$tmp_dir/out.builtin.json"

if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$(fix_path "$tmp_dir/out.builtin.json")" > /dev/null
elif command -v python >/dev/null 2>&1; then
  python -m json.tool "$(fix_path "$tmp_dir/out.builtin.json")" > /dev/null
fi
grep -q '"animations": \[' "$tmp_dir/out.builtin.json"
grep -q '"sprites": \[' "$tmp_dir/out.builtin.json"
grep -q '"name": "a"' "$tmp_dir/out.builtin.json"
# Marker fields appear on separate lines in pretty-printed JSON output
grep -q '"name": "hit"' "$tmp_dir/out.builtin.json"
grep -q '"type": "point"' "$tmp_dir/out.builtin.json"
grep -q '"radius": 4' "$tmp_dir/out.builtin.json"
grep -q '"name": "run"' "$tmp_dir/out.builtin.json"
grep -q '"fps": 8' "$tmp_dir/out.builtin.json"
# sprite_indexes and sprite_names are pretty-printed arrays; check field presence and values
grep -q '"sprite_indexes"' "$tmp_dir/out.builtin.json"
grep -q '"sprite_names"' "$tmp_dir/out.builtin.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
run = next(a for a in d['animations'] if a['name'] == 'run')
assert run['sprite_indexes'] == [0, 1], 'sprite_indexes mismatch'
assert run['sprite_names'] == ['a', 'b'], 'sprite_names mismatch'
a_sp = next(s for s in d['sprites'] if s['name'] == 'a')
hit = next(m for m in a_sp['markers'] if m['name'] == 'hit')
assert hit['type'] == 'point' and hit['x'] == 3 and hit['y'] == 5, 'hit marker mismatch'
hurt = next(m for m in a_sp['markers'] if m['name'] == 'hurt')
assert hurt['type'] == 'circle' and hurt['x'] == 6 and hurt['y'] == 7 and hurt['radius'] == 4, 'hurt marker mismatch'
" "$(fix_path "$tmp_dir/out.builtin.json")"
fi
# Both atlases and animations sections must be present (alphabetical order puts animations first)
grep -q '"atlases"' "$tmp_dir/out.builtin.json"
grep -q '"animations"' "$tmp_dir/out.builtin.json"

# ── Raw transform (sprat-lib's input) ────────────────────────────────────────
raw_layout_file="$tmp_dir/raw.layout.txt"
cat > "$raw_layout_file" <<'LAYOUT'
atlas 64,32
scale 1
sprite "./frames/a.png" 0,0 16,16
sprite "./frames/b.png" 16,0 8,8 1,2 3,4 rotated
sprite "./frames/c dé.png" 24,0 8,8 slice=1,2,3,4,repeat,mirror
LAYOUT
raw_markers_file="$tmp_dir/raw.markers.txt"
cat > "$raw_markers_file" <<'MARKERS'
path "./frames/a.png"
- marker "hit" point 3,5
- marker "hurt" circle 6,7 4
- marker "zone" polygon 0,0 4,0 2,3
path "b"
- marker "foot" rectangle 1,2 3,4
MARKERS
raw_animations_file="$tmp_dir/raw.animations.txt"
cat > "$raw_animations_file" <<'ANIMS'
animation "run" 8
- frame "./frames/a.png"
- frame "b"
animation "run-left" alias "run" flip hv
ANIMS
# String lengths count UTF-8 bytes ("c dé" is 5); an alias points at its source by index.
# The sheet line totals 4 markers, 3 vertices, 2 frames and 79 string bytes:
# sprites 1+14 + 1+14 + 5+18, markers 3+4+4+4, animations 3+8.
cat > "$tmp_dir/expected.sprat" <<'RAW'
sprat-raw 1
sheet 1 0 0 0 1 3 2 4 3 2 79 0: 0:
atlas 64 32 0: 0: 0:
sprite 0 0 0 16 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 3 1:a 14:./frames/a.png 0:
marker 0 3 5 0 0 0 0 3:hit
marker 1 6 7 4 0 0 0 4:hurt
marker 3 0 0 0 0 0 3 0 0 4 0 2 3 4:zone
sprite 0 16 0 8 8 90 1 2 3 4 0 0 0 0 0 0 0 0 0 1 1:b 14:./frames/b.png 0:
marker 2 1 2 0 3 4 0 4:foot
sprite 0 24 0 8 8 0 0 0 0 0 0 0 1 1 2 3 4 1 2 0 5:c dé 18:./frames/c dé.png 0:
anim 8 0 -1 2 0 1 3:run
anim 8 3 0 0 8:run-left
RAW
"$convert_bin" --transform raw --markers "$(fix_path "$raw_markers_file")" \
  --animations "$(fix_path "$raw_animations_file")" < "$raw_layout_file" > "$tmp_dir/out.sprat"
if ! diff "$tmp_dir/expected.sprat" "$tmp_dir/out.sprat"; then
  echo "raw transform output mismatch" >&2
  exit 1
fi

printf 'animation "x" alias "nope"\n' > "$tmp_dir/raw.bad_alias.txt"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/raw.bad_alias.txt")" \
    < "$raw_layout_file" > /dev/null 2> "$tmp_dir/raw.bad_alias.err"; then
  echo "raw transform accepted an alias of an unknown animation" >&2
  exit 1
fi
grep -q "unknown animation 'nope'" "$tmp_dir/raw.bad_alias.err"

# ── JSON auto-escape transform (Jsonnet) ─────────────────────────────────────
json_auto_escape_transform="$tmp_dir/json_auto_escape.jsonnet"
cat > "$json_auto_escape_transform" <<'JSONAUTO'
local sprat = std.extVar("sprat");

local sprite_part(s) =
  '{"name":' + std.manifestJson(s.name) + ',"path":' + std.manifestJson(s.path) + '}';

local marker_part(m) =
  '{"name":' + std.manifestJson(m.name) + ',"type":' + std.manifestJson(m.type) + '}';

{
  name: "json_auto_escape",
  extension: ".json",
  content:
    '{"markers":[\n' +
    std.join(",\n", [marker_part(m) for m in sprat.markers]) +
    '\n],"sprites":[\n' +
    std.join(",\n", [sprite_part(s) for s in sprat.sprites]) +
    '\n]}',
}
JSONAUTO

markers_quotes_file="$tmp_dir/markers.quotes.txt"
cat > "$markers_quotes_file" <<'MARKERSQ'
path "./frames/a\"q.png"
- marker "hit\"zone" point 1,2
MARKERSQ

"$convert_bin" --transform "$(fix_path "$json_auto_escape_transform")" --markers "$(fix_path "$markers_quotes_file")" < "$layout_quotes_file" > "$tmp_dir/out.json.autoescape"
if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$(fix_path "$tmp_dir/out.json.autoescape")" > /dev/null
elif command -v python >/dev/null 2>&1; then
  python -m json.tool "$(fix_path "$tmp_dir/out.json.autoescape")" > /dev/null
fi
grep -Fq '"name":"a\"q"' "$tmp_dir/out.json.autoescape"
grep -Fq '"path":"./frames/a\"q.png"' "$tmp_dir/out.json.autoescape"
grep -Fq '"name":"hit\"zone"' "$tmp_dir/out.json.autoescape"

# --- Animation alias test ---
animations_alias_file="$tmp_dir/animations_alias.txt"
cat > "$animations_alias_file" <<'ALIASANIMS'
animation "run"
- frame "./frames/a.png"
- frame "b"
animation "idle"
- frame 1
animation "run-alias" alias "run" flip h
ALIASANIMS

"$convert_bin" --transform json --animations "$(fix_path "$animations_alias_file")" < "$layout_file" > "$tmp_dir/out.alias.json"
grep -q '"name": "run-alias"' "$tmp_dir/out.alias.json"
grep -q '"alias": "run"' "$tmp_dir/out.alias.json"
grep -q '"flip": "h"' "$tmp_dir/out.alias.json"
if grep -q '"flip": "v' "$tmp_dir/out.alias.json"; then
  echo "alias with h-only flip should not have v in flip value" >&2
  exit 1
fi
# With pretty-printed JSON, use python3 to validate alias vs regular animation fields
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
alias = next(a for a in d['animations'] if a['name'] == 'run-alias')
assert 'fps' not in alias, 'alias entry should not have fps field'
assert 'sprite_indexes' not in alias, 'alias entry should not have sprite_indexes field'
run = next(a for a in d['animations'] if a['name'] == 'run')
assert run['fps'] == 8, 'run fps mismatch'
assert run['sprite_indexes'] == [0, 1], 'run sprite_indexes mismatch'
" "$(fix_path "$tmp_dir/out.alias.json")"
fi
# regular animations unaffected
grep -q '"name": "run"' "$tmp_dir/out.alias.json"
grep -q '"fps": 8' "$tmp_dir/out.alias.json"
grep -q '"sprite_indexes"' "$tmp_dir/out.alias.json"

# ── Aseprite non-contiguous animation (consecutive_runs) ─────────────────────
noncontig_layout="$tmp_dir/noncontig.txt"
cat > "$noncontig_layout" <<'NCLAYOUT'
atlas 128,16
scale 1
sprite "f0.png" 0,0 16,16
sprite "f1.png" 16,0 16,16
sprite "f2.png" 32,0 16,16
sprite "f3.png" 48,0 16,16
sprite "f4.png" 64,0 16,16
NCLAYOUT

noncontig_anims="$tmp_dir/noncontig_anims.txt"
cat > "$noncontig_anims" <<'NCANIMS'
animation "jump"
- frame "f0.png"
- frame "f2.png"
- frame "f4.png"
animation "walk"
- frame "f0.png"
- frame "f1.png"
- frame "f2.png"
NCANIMS

"$convert_bin" --transform aseprite --animations "$(fix_path "$noncontig_anims")" < "$noncontig_layout" > "$tmp_dir/out.aseprite.nc.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
tags = d['meta']['frameTags']
jump_tags = [t for t in tags if t['name'] == 'jump']
assert len(jump_tags) == 3, 'jump: expected 3 frameTags, got ' + str(len(jump_tags))
assert jump_tags[0]['from'] == 0 and jump_tags[0]['to'] == 0, 'jump tag 0 mismatch'
assert jump_tags[1]['from'] == 2 and jump_tags[1]['to'] == 2, 'jump tag 1 mismatch'
assert jump_tags[2]['from'] == 4 and jump_tags[2]['to'] == 4, 'jump tag 2 mismatch'
walk_tags = [t for t in tags if t['name'] == 'walk']
assert len(walk_tags) == 1, 'walk: expected 1 frameTag, got ' + str(len(walk_tags))
assert walk_tags[0]['from'] == 0 and walk_tags[0]['to'] == 2, 'walk tag mismatch'
" "$(fix_path "$tmp_dir/out.aseprite.nc.json")"
fi

# ── Multi-atlas (multipack) layout ───────────────────────────────────────────
multipack_layout="$tmp_dir/multipack.txt"
cat > "$multipack_layout" <<'MPLAYOUT'
multipack true
atlas 32,32
scale 1
sprite "p0.png" 0,0 16,16
atlas 32,32
sprite "p1.png" 0,0 16,16
MPLAYOUT

"$convert_bin" --transform json --atlas 'atlas_%d.png' < "$multipack_layout" > "$tmp_dir/out.multipack.json"
grep -q '"multipack": true' "$tmp_dir/out.multipack.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
assert d['multipack'] == True, 'multipack should be true'
assert len(d['atlases']) == 2, 'expected 2 atlases, got ' + str(len(d['atlases']))
p0 = next(s for s in d['sprites'] if s['name'] == 'p0')
p1 = next(s for s in d['sprites'] if s['name'] == 'p1')
assert p0['atlas_index'] == 0, 'p0 should be atlas_index 0, got ' + str(p0['atlas_index'])
assert p1['atlas_index'] == 1, 'p1 should be atlas_index 1, got ' + str(p1['atlas_index'])
" "$(fix_path "$tmp_dir/out.multipack.json")"
fi

# ── Animation/marker files with root + relative paths ─────────────────────────
# Regression: on Windows the root was joined with '\', so suffix lookup failed and
# frames were silently dropped. The backslash variant exercises that on all OSes.
rootrel_layout="$tmp_dir/rootrel_layout.txt"
cat > "$rootrel_layout" <<'RRLAYOUT'
root "./frames"
atlas 8,4
sprite "a.png" 0,0 4,4
sprite "b.png" 4,0 4,4
RRLAYOUT
rootrel_anims="$tmp_dir/rootrel_anims.txt"
cat > "$rootrel_anims" <<'RRANIMS'
root "./frames"
animation "run" 12
- frame "a.png"
- frame b.png
animation "back" 6
- frame "frames\b.png"
- frame ".\frames\a.png"
RRANIMS
rootrel_markers="$tmp_dir/rootrel_markers.txt"
cat > "$rootrel_markers" <<'RRMARKERS'
root "./frames"
path "b.png"
- marker "hit" point 1,2
RRMARKERS
"$convert_bin" --transform phaser-anims --atlas atlas.png --animations "$(fix_path "$rootrel_anims")" < "$rootrel_layout" > "$tmp_dir/out.rootrel.phaser.json"
"$convert_bin" --transform json --animations "$(fix_path "$rootrel_anims")" --markers "$(fix_path "$rootrel_markers")" < "$rootrel_layout" > "$tmp_dir/out.rootrel.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
p = json.load(open(sys.argv[1]))
run = next(a for a in p['anims'] if a['key'] == 'run')
assert run['frameRate'] == 12, run
assert len(run['frames']) == 2, 'root-relative frames dropped: %r' % run
d = json.load(open(sys.argv[2]))
run = next(a for a in d['animations'] if a['name'] == 'run')
assert run['sprite_indexes'] == [0, 1], run
back = next(a for a in d['animations'] if a['name'] == 'back')
assert back['sprite_indexes'] == [1, 0], 'backslash frame paths not resolved: %r' % back
b_sp = next(s for s in d['sprites'] if s['name'] == 'b')
assert any(m['name'] == 'hit' for m in b_sp['markers']), 'root-relative marker path not resolved'
" "$(fix_path "$tmp_dir/out.rootrel.phaser.json")" "$(fix_path "$tmp_dir/out.rootrel.json")"
fi

# ── Layout aliases are expanded into frames ───────────────────────────────────
alias_layout="$tmp_dir/alias_layout.txt"
cat > "$alias_layout" <<'ALIASLAYOUT'
atlas 8,8
sprite "idle_10.png" 0,0 4,4 1,6 2,0
sprite "idle_2.png" 4,0 4,4
alias "idle_1.png" "idle_10.png"
alias "idle_3.png" "idle_10.png" 1,0 2,6
ALIASLAYOUT
"$convert_bin" --transform phaser-hash --atlas atlas.png < "$alias_layout" > "$tmp_dir/out.alias.phaser.json"
"$convert_bin" --transform json --auto-animations < "$alias_layout" > "$tmp_dir/out.alias_sprites.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
frames = json.load(open(sys.argv[1]))['frames']
assert sorted(frames) == ['idle_1', 'idle_10', 'idle_2', 'idle_3'], sorted(frames)
canonical = frames['idle_10']
for name in ('idle_1', 'idle_3'):
    assert frames[name]['frame'] == canonical['frame'], name
    assert frames[name]['sourceSize'] == canonical['sourceSize'], name
# idle_1 shares the canonical placement; idle_3 keeps its own (6px higher)
assert frames['idle_1']['spriteSourceSize'] == canonical['spriteSourceSize']
assert frames['idle_3']['spriteSourceSize']['y'] == 0, frames['idle_3']
assert canonical['spriteSourceSize']['y'] == 6, canonical
d = json.load(open(sys.argv[2]))
by_name = {s['name']: s for s in d['sprites']}
assert by_name['idle_1']['alias_of'] == 'idle_10.png', by_name['idle_1']
assert 'alias_of' not in by_name['idle_2'], by_name['idle_2']
# auto-detected frames play in natural name order, not layout order
idle = next(a for a in d['animations'] if a['name'] == 'idle')
assert idle['sprite_names'] == ['idle_1', 'idle_2', 'idle_3', 'idle_10'], idle
" "$(fix_path "$tmp_dir/out.alias.phaser.json")" "$(fix_path "$tmp_dir/out.alias_sprites.json")"
fi
printf 'atlas 4,4\nsprite "a.png" 0,0 4,4\nalias "b.png" "missing.png"\n' > "$tmp_dir/alias_missing.txt"
if "$convert_bin" --transform json < "$tmp_dir/alias_missing.txt" > /dev/null 2> "$tmp_dir/alias_missing.err"; then
  echo "alias to an unknown sprite should fail" >&2
  exit 1
fi
grep -q 'missing.png' "$tmp_dir/alias_missing.err"

# ── Empty animations file ─────────────────────────────────────────────────────
empty_anims_file="$tmp_dir/empty_anims.txt"
cat > "$empty_anims_file" <<'EMPTYANIMS'
fps 12
# no animation definitions follow
EMPTYANIMS

"$convert_bin" --transform "$(fix_path "$iter_transform")" --animations "$(fix_path "$empty_anims_file")" < "$layout_file" > "$tmp_dir/out.iter.emptyanims"
grep -q '^A_EMPTY$' "$tmp_dir/out.iter.emptyanims"
if grep -q '^A_ON$' "$tmp_dir/out.iter.emptyanims"; then
  echo "empty animations file should not activate animation branch" >&2
  exit 1
fi
"$convert_bin" --transform json --animations "$(fix_path "$empty_anims_file")" < "$layout_file" > "$tmp_dir/out.json.emptyanims"
if grep -q '"animations"' "$tmp_dir/out.json.emptyanims"; then
  echo "empty animations file should not produce animations key in JSON output" >&2
  exit 1
fi
grep -q '"sprites"' "$tmp_dir/out.json.emptyanims"

# --- --output-dir group mode test ---
# Create two lightweight group transforms in the active transforms directory.
transforms_dir="$("$convert_bin" --transforms-dir)"
grp_a="$transforms_dir/tstsuite.txt.jsonnet"
grp_b="$transforms_dir/tstsuite.json.jsonnet"
trap 'rm -rf "$tmp_dir"; rm -f "$grp_a" "$grp_b"' EXIT

cat > "$grp_a" <<'GRPA'
local sprat = std.extVar("sprat");
{
  name: "tstsuite_txt",
  extension: ".txt",
  content: "stem=" + sprat.output_stem + "\n" +
           std.join("\n", [s.name for s in sprat.sprites]) + "\n",
}
GRPA

cat > "$grp_b" <<'GRPB'
local sprat = std.extVar("sprat");
{
  name: "tstsuite_json",
  extension: ".json",
  content: '{"stem":"' + sprat.output_stem + '","sprites":[' +
           std.join(",", ['"' + s.name + '"' for s in sprat.sprites]) + ']}',
}
GRPB

group_out="$tmp_dir/group_out"
"$convert_bin" --transform tstsuite --output-dir "$(fix_path "$group_out")" < "$layout_file"
test -f "$group_out/txt.txt"
test -f "$group_out/json.json"
grep -q 'stem=txt' "$group_out/txt.txt"
grep -q '"stem":"json"' "$group_out/json.json"

# --output-dir single mode: write to file using stem-derived name
single_out="$tmp_dir/single_out"
"$convert_bin" --transform tstsuite.json --output-dir "$(fix_path "$single_out")" < "$layout_file"
test -f "$single_out/json.json"
grep -q '"stem":"json"' "$single_out/json.json"

# ── Nine-slice transform output ───────────────────────────────────────────────
sliced_layout="$tmp_dir/sliced.txt"
cat > "$sliced_layout" <<'SLICEDLAYOUT'
atlas 64,64
scale 1
sprite "panel.png" 0,0 32,32 slice=8,8,8,8
sprite "border.png" 32,0 16,16 slice=4,4,4,4,repeat,stretch
sprite "plain.png" 0,32 16,16
SLICEDLAYOUT

# JSON: has_slice flag and slice fields
"$convert_bin" --transform json < "$sliced_layout" > "$tmp_dir/out.sliced.json"
grep -q '"has_slice": true' "$tmp_dir/out.sliced.json"
grep -q '"has_slice": false' "$tmp_dir/out.sliced.json"
grep -q '"slice"' "$tmp_dir/out.sliced.json"
grep -q '"h_mode": "stretch"' "$tmp_dir/out.sliced.json"
grep -q '"h_mode": "repeat"' "$tmp_dir/out.sliced.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
panel = next(s for s in d['sprites'] if s['name'] == 'panel')
assert panel['has_slice'] == True
assert panel['slice']['left'] == 8 and panel['slice']['top'] == 8
assert panel['slice']['right'] == 8 and panel['slice']['bottom'] == 8
assert panel['slice']['h_mode'] == 'stretch' and panel['slice']['v_mode'] == 'stretch'
plain = next(s for s in d['sprites'] if s['name'] == 'plain')
assert plain['has_slice'] == False
assert 'slice' not in plain
" "$(fix_path "$tmp_dir/out.sliced.json")"
fi

# XML: slice attributes present on sliced sprites
"$convert_bin" --transform xml < "$sliced_layout" > "$tmp_dir/out.sliced.xml"
grep -q 'slice_left="8"' "$tmp_dir/out.sliced.xml"
grep -q 'slice_h="stretch"' "$tmp_dir/out.sliced.xml"
grep -q 'slice_h="repeat"' "$tmp_dir/out.sliced.xml"
if grep -q 'slice_left' "$tmp_dir/out.sliced.xml"; then
  # plain sprite line must not contain slice attributes
  if grep 'name="plain"' "$tmp_dir/out.sliced.xml" | grep -q 'slice_left'; then
    echo "plain sprite should not have slice_left in XML" >&2
    exit 1
  fi
fi

# CSV: has_slice column values
"$convert_bin" --transform csv < "$sliced_layout" > "$tmp_dir/out.sliced.csv"
grep -q ',true,8,8,8,8,stretch,stretch$' "$tmp_dir/out.sliced.csv"
grep -q ',true,4,4,4,4,repeat,stretch$' "$tmp_dir/out.sliced.csv"
grep -q ',false,,,,,,$' "$tmp_dir/out.sliced.csv"

# unity.json: border field present for sliced sprites
"$convert_bin" --transform unity.json < "$sliced_layout" > "$tmp_dir/out.sliced.unity.json"
grep -q '"border"' "$tmp_dir/out.sliced.unity.json"
grep -q '"l": 8' "$tmp_dir/out.sliced.unity.json"

# unity.meta: per-sprite border YAML line
"$convert_bin" --transform unity.meta < "$sliced_layout" > "$tmp_dir/out.sliced.meta"
grep -q 'border: {x: 8, y: 8, z: 8, w: 8}' "$tmp_dir/out.sliced.meta"
grep -q 'border: {x: 0, y: 0, z: 0, w: 0}' "$tmp_dir/out.sliced.meta"

# libgdx: split line present for sliced sprites
"$convert_bin" --transform libgdx < "$sliced_layout" > "$tmp_dir/out.sliced.atlas"
grep -q 'split: 8, 8, 8, 8' "$tmp_dir/out.sliced.atlas"
if grep -A6 '^plain$' "$tmp_dir/out.sliced.atlas" | grep -q 'split'; then
  echo "plain sprite should not have split in libgdx output" >&2
  exit 1
fi

# phaser-hash: border field present for sliced sprites
"$convert_bin" --transform phaser-hash < "$sliced_layout" > "$tmp_dir/out.sliced.phaser.json"
grep -q '"border"' "$tmp_dir/out.sliced.phaser.json"
grep -q '"l": 8' "$tmp_dir/out.sliced.phaser.json"

# aseprite: slices[] populated from sliced sprites
"$convert_bin" --transform aseprite < "$sliced_layout" > "$tmp_dir/out.sliced.aseprite.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
slices = d['meta']['slices']
assert len(slices) == 2, 'expected 2 slices, got ' + str(len(slices))
panel_sl = next(sl for sl in slices if sl['name'] == 'panel')
assert panel_sl['keys'][0]['frame'] == 0, 'panel slice frame mismatch'
assert panel_sl['keys'][0]['center']['x'] == 8
assert panel_sl['keys'][0]['center']['y'] == 8
assert panel_sl['keys'][0]['center']['w'] == 16  # 32 - 8 - 8
assert panel_sl['keys'][0]['center']['h'] == 16
plain_names = [sl['name'] for sl in slices]
assert 'plain' not in plain_names, 'plain sprite should not appear in slices'
" "$(fix_path "$tmp_dir/out.sliced.aseprite.json")"
fi

# godot: slice object present for sliced sprites, absent for plain
"$convert_bin" --transform godot < "$sliced_layout" > "$tmp_dir/out.sliced.godot.json"
grep -q '"slice"' "$tmp_dir/out.sliced.godot.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
panel = next(f for f in d['frames'] if f['name'] == 'panel')
assert panel['slice']['left'] == 8 and panel['slice']['top'] == 8
assert panel['slice']['h_mode'] == 'stretch'
plain = next(f for f in d['frames'] if f['name'] == 'plain')
assert 'slice' not in plain, 'plain sprite should not have slice'
" "$(fix_path "$tmp_dir/out.sliced.godot.json")"
fi

# plist: capInsets present for sliced sprites, absent for plain
"$convert_bin" --transform plist < "$sliced_layout" > "$tmp_dir/out.sliced.plist"
grep -q '<key>capInsets</key>' "$tmp_dir/out.sliced.plist"
grep -q '{{8,8},{16,16}}' "$tmp_dir/out.sliced.plist"
if grep -A8 '<key>plain</key>' "$tmp_dir/out.sliced.plist" | grep -q 'capInsets'; then
  echo "plain sprite should not have capInsets in plist" >&2
  exit 1
fi

# Nine-patch sprite naming: "button.9.png" should produce name "button", not "button.9"
nine_patch_layout="$tmp_dir/ninepatch_name.txt"
cat > "$nine_patch_layout" <<'LAYOUT'
atlas 32,32
scale 1
sprite "button.9.png" 0,0 30,30 slice=4,4,4,4
LAYOUT
"$convert_bin" --transform json < "$nine_patch_layout" > "$tmp_dir/ninepatch_name.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
name = d['sprites'][0]['name']
assert name == 'button', 'expected name \"button\", got \"' + name + '\"'
" "$(fix_path "$tmp_dir/ninepatch_name.json")"
fi

# libgdx: pages are separated by a blank line and rotated regions use the unrotated size
cat > "$tmp_dir/libgdx_multi.txt" <<'EOF'
multipack true
rotation ccw
atlas 100,50
sprite "rot.png" 0,0 30,50 rotated
atlas 10,10
sprite "plain.png" 0,0 10,10
EOF
"$convert_bin" --transform libgdx -a 'atlas_%d.png' < "$tmp_dir/libgdx_multi.txt" | tr -d '\r' > "$tmp_dir/out.multi.atlas"
if [ -n "$(grep -B1 '^atlas_1.png$' "$tmp_dir/out.multi.atlas" | head -n1 | grep -v '^atlas_1.png$')" ]; then
  echo "libgdx pages must be separated by a blank line" >&2
  cat "$tmp_dir/out.multi.atlas" >&2
  exit 1
fi
grep -A3 '^rot$' "$tmp_dir/out.multi.atlas" | grep -q '  size: 50, 30'
grep -A4 '^rot$' "$tmp_dir/out.multi.atlas" | grep -q '  orig: 50, 30'

# rotation direction: formats with a fixed convention refuse the other direction
if sed '/^rotation ccw$/d' "$tmp_dir/libgdx_multi.txt" | "$convert_bin" --transform libgdx > /dev/null 2>&1; then
  echo "libgdx must refuse clockwise-rotated sprites" >&2
  exit 1
fi
for t in phaser-hash phaser-array aseprite plist godot unity.json; do
  if "$convert_bin" --transform "$t" < "$tmp_dir/libgdx_multi.txt" > /dev/null 2>&1; then
    echo "$t must refuse counter-clockwise-rotated sprites" >&2
    exit 1
  fi
  # two atlases: per-atlas formats write one file each, so give them an output directory
  sed '/^rotation ccw$/d' "$tmp_dir/libgdx_multi.txt" | "$convert_bin" --transform "$t" --output-dir "$tmp_dir/cw_$t" > /dev/null
done
# layouts without rotated sprites work with any format regardless of direction
printf 'rotation ccw\natlas 4,4\nsprite "a.png" 0,0 4,4\n' | "$convert_bin" --transform phaser-hash > /dev/null
"$convert_bin" --transform json < "$tmp_dir/libgdx_multi.txt" | grep -q '"rotation": 270'
"$convert_bin" --transform csv < "$tmp_dir/libgdx_multi.txt" | grep -q ',270,'
"$convert_bin" --transform xml < "$tmp_dir/libgdx_multi.txt" | grep -q 'rotation="270"'
sed '/^rotation ccw$/d' "$tmp_dir/libgdx_multi.txt" | "$convert_bin" --transform json | grep -q '"rotation": 90'
"$convert_bin" --transform css < "$tmp_dir/libgdx_multi.txt" | grep -q 'rotate(90deg) translate(0, -100%)'

# css: quotes in the atlas path and comment terminators in paths are escaped
printf 'atlas 4,4\nsprite "a*/b.png" 0,0 4,4\n' > "$tmp_dir/css_escape.txt"
"$convert_bin" --transform css -a "it's.png" < "$tmp_dir/css_escape.txt" > "$tmp_dir/out.escape.css"
grep -qF "url('it\\'s.png')" "$tmp_dir/out.escape.css"
if grep -qF 'a*/b.png' "$tmp_dir/out.escape.css"; then
  echo "css output must not contain a raw comment terminator" >&2
  exit 1
fi

# csv: markers of every type export, and the markers_json column stays one quoted field
printf 'atlas 16,16\nsprite "a.png" 0,0 16,16\n' > "$tmp_dir/csv_markers.txt"
cat > "$tmp_dir/csv_markers.markers" <<'MARKERS'
path "a.png"
- marker "hurt" circle 6,7 4
- marker "hit" point 1,1
- marker "poly" polygon 0,0 4,0 4,4
MARKERS
"$convert_bin" --transform csv --markers "$(fix_path "$tmp_dir/csv_markers.markers")" < "$tmp_dir/csv_markers.txt" > "$tmp_dir/out.markers.csv"
grep -qF ',3,"[{""name"":""hurt""' "$tmp_dir/out.markers.csv"
grep -q '^marker,0,hurt,circle,6,7,4,,,,0,a,a.png' "$tmp_dir/out.markers.csv"
grep -q '^marker,1,hit,point,1,1,,,,,0,a,a.png' "$tmp_dir/out.markers.csv"
grep -q '^marker,2,poly,polygon,0,0,,,,"0,0|4,0|4,4",0,a,a.png' "$tmp_dir/out.markers.csv"

# TexturePacker-style formats: rotated frames carry the unrotated size (readers swap it),
# and plist rects use Cocos2d-x's "{{x,y},{w,h}}" syntax.
printf 'atlas 64,32\nsprite "b.png" 16,0 10,6 1,2 3,4 rotated\n' > "$tmp_dir/tp_rotated.txt"
for t in phaser-hash phaser-array unity.json aseprite; do
  "$convert_bin" --transform "$t" < "$tmp_dir/tp_rotated.txt" | tr -d ' \n' > "$tmp_dir/out.tp.$t"
  grep -qF '"frame":{"h":10,"w":6,"x":16,"y":0}' "$tmp_dir/out.tp.$t"
done
"$convert_bin" --transform plist < "$tmp_dir/tp_rotated.txt" > "$tmp_dir/out.tp.plist"
grep -qF '<string>{{16,0},{6,10}}</string>' "$tmp_dir/out.tp.plist"
grep -qF '<string>{{1,2},{6,10}}</string>' "$tmp_dir/out.tp.plist"
grep -qF '<string>{-1,1}</string>' "$tmp_dir/out.tp.plist"
# odd trim differences give a half-pixel offset instead of rounding it away
printf 'atlas 8,8\nsprite "c.png" 0,0 4,4 1,0 0,0\n' | "$convert_bin" --transform plist | grep -qF '<string>{0.5,0}</string>'

# godot: animations list their frames in play order; from/to only for one ascending run
printf 'atlas 24,8\nsprite "0.png" 0,0 8,8\nsprite "1.png" 8,0 8,8\nsprite "2.png" 16,0 8,8\n' > "$tmp_dir/godot_anim.txt"
printf 'animation "back"\n- frame 2\n- frame 0\nanimation "run"\n- frame 0\n- frame 1\n' > "$tmp_dir/godot_anim.anims"
"$convert_bin" --transform godot --animations "$(fix_path "$tmp_dir/godot_anim.anims")" < "$tmp_dir/godot_anim.txt" | tr -d ' \n' > "$tmp_dir/out.godot_anim"
grep -qF '{"frames":[2,0],"loop":true,"name":"back","speed":8}' "$tmp_dir/out.godot_anim"
grep -qF '{"frames":[0,1],"from":0,"loop":true,"name":"run","speed":8,"to":1}' "$tmp_dir/out.godot_anim"

# multipack: single-texture formats write one file per atlas holding only that atlas's sprites
printf 'atlas 32,32\nsprite "a.png" 0,0 16,16\nsprite "b.png" 16,0 8,8 slice=2,2,2,2\natlas 64,16\nsprite "c.png" 0,0 8,8\nsprite "d.png" 8,0 8,8\n' > "$tmp_dir/mp.txt"
printf 'animation "walk"\n- frame 0\n- frame 1\n- frame 2\n- frame 3\n' > "$tmp_dir/mp.anims"
mp_anims="$(fix_path "$tmp_dir/mp.anims")"
if "$convert_bin" --transform plist < "$tmp_dir/mp.txt" > /dev/null 2>&1; then
  echo "multipack plist must require --output-dir" >&2
  exit 1
fi
"$convert_bin" --transform plist --output-dir "$tmp_dir/mp_plist" < "$tmp_dir/mp.txt"
grep -qF '<string>atlas_1.png</string>' "$tmp_dir/mp_plist/atlas_1.plist"
grep -qF '<key>c</key>' "$tmp_dir/mp_plist/atlas_1.plist"
if grep -qF '<key>a</key>' "$tmp_dir/mp_plist/atlas_1.plist"; then
  echo "atlas_1.plist must only list atlas 1 sprites" >&2
  exit 1
fi
"$convert_bin" --transform unity.json --output-dir "$tmp_dir/mp_unity_json" < "$tmp_dir/mp.txt"
grep -qF '"image": "atlas_1.png"' "$tmp_dir/mp_unity_json/atlas_1.json"
grep -qF '"d": {' "$tmp_dir/mp_unity_json/atlas_1.json"
# aseprite: tags and slice keys use frame positions inside each file; a run split across
# atlases becomes one tag per file
"$convert_bin" --transform aseprite --animations "$mp_anims" --output-dir "$tmp_dir/mp_aseprite" < "$tmp_dir/mp.txt"
tr -d ' \n' < "$tmp_dir/mp_aseprite/atlas_0.json" > "$tmp_dir/mp_aseprite.0"
tr -d ' \n' < "$tmp_dir/mp_aseprite/atlas_1.json" > "$tmp_dir/mp_aseprite.1"
grep -qF '"frameTags":[{"direction":"forward","from":0,"name":"walk","to":1}]' "$tmp_dir/mp_aseprite.0"
grep -qF '"frameTags":[{"direction":"forward","from":0,"name":"walk","to":1}]' "$tmp_dir/mp_aseprite.1"
grep -qF '"frame":1}],"name":"b"' "$tmp_dir/mp_aseprite.0"
# phaser: one multi-atlas JSON on stdout
for t in phaser-hash phaser-array; do
  "$convert_bin" --transform "$t" < "$tmp_dir/mp.txt" | tr -d ' \n' > "$tmp_dir/out.mp.$t"
  grep -qF '"image":"atlas_1.png","scale":1,"size":{"h":16,"w":64}' "$tmp_dir/out.mp.$t"
  grep -qF '"textures":[{"format":"RGBA8888","frames":[{"filename":"a"' "$tmp_dir/out.mp.$t"
done
# godot: one file listing atlases, each frame tagged with its atlas
"$convert_bin" --transform godot < "$tmp_dir/mp.txt" | tr -d ' \n' > "$tmp_dir/out.mp.godot"
grep -qF '"atlases":[{"image":"atlas_0.png","size":{"h":32,"w":32}},{"image":"atlas_1.png","size":{"h":16,"w":64}}]' "$tmp_dir/out.mp.godot"
grep -qF '{"atlas":1,"margin"' "$tmp_dir/out.mp.godot"

# multi-file output: filenames must stay inside --output-dir and be unique; a refused result
# writes nothing
printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' > "$tmp_dir/one.txt"
printf '{ name: "escape", extension: ".x", files: [{ filename: "ok.x", content: "1" }, { filename: "../escaped.x", content: "2" }] }\n' > "$tmp_dir/escape.jsonnet"
printf '{ name: "dupe", extension: ".x", files: [{ filename: "a.x", content: "1" }, { filename: "a.x", content: "2" }] }\n' > "$tmp_dir/dupe.jsonnet"
for t in escape dupe; do
  if "$convert_bin" --transform "$(fix_path "$tmp_dir/$t.jsonnet")" --output-dir "$tmp_dir/files_$t/out" < "$tmp_dir/one.txt" > /dev/null 2>&1; then
    echo "$t.jsonnet output must be refused" >&2
    exit 1
  fi
  if [ -e "$tmp_dir/files_$t" ]; then
    echo "$t.jsonnet left output behind" >&2
    exit 1
  fi
done

# phaser-anims: frames use the texture key, shared by all atlases of a multipack layout
printf 'animation "walk"\n- frame 0\n- frame 1\n' > "$tmp_dir/key.anims"
key_anims="$(fix_path "$tmp_dir/key.anims")"
"$convert_bin" --transform phaser-anims --animations "$key_anims" -a 'out/hero_%d.png' < "$tmp_dir/mp.txt" | grep -qF '"key": "hero"'
"$convert_bin" --transform phaser-anims --animations "$key_anims" < "$tmp_dir/mp.txt" | grep -qF '"key": "atlas"'
printf 'atlas 16,8\nsprite "a.png" 0,0 8,8\nsprite "b.png" 8,0 8,8\n' \
  | "$convert_bin" --transform phaser-anims --animations "$key_anims" -a 'hero.png' | grep -qF '"key": "hero"'

# scaled layouts: trims and markers are in original-image pixels, the packed size is scaled;
# exported metadata uses atlas pixels throughout
printf 'scale 0.5\natlas 200,250\nsprite "f.png" 0,0 161,228 4,5 2,0\n' > "$tmp_dir/scaled.txt"
printf 'path "f.png"\n- marker "pivot" point 164,230\n- marker "hit" rectangle 10,20 30,40\n' > "$tmp_dir/scaled.markers"
"$convert_bin" --transform phaser-hash < "$tmp_dir/scaled.txt" | tr -d ' \n' > "$tmp_dir/out.scaled.phaser"
grep -qF '"sourceSize":{"h":231,"w":164}' "$tmp_dir/out.scaled.phaser"
grep -qF '"spriteSourceSize":{"h":228,"w":161,"x":2,"y":3}' "$tmp_dir/out.scaled.phaser"
"$convert_bin" --transform json --markers "$(fix_path "$tmp_dir/scaled.markers")" < "$tmp_dir/scaled.txt" | tr -d ' \n' > "$tmp_dir/out.scaled.json"
grep -qF '"pivot":{"x":82,"y":115}' "$tmp_dir/out.scaled.json"
grep -qF '"type":"rectangle","w":15,"x":5,"y":10}' "$tmp_dir/out.scaled.json"
grep -qF '"h":20,"index":1,"name":"hit"' "$tmp_dir/out.scaled.json"

# phaser applies any pivot as the origin: only write one when a pivot marker sets it
printf 'atlas 8,8\nsprite "a.png" 0,0 8,8\n' > "$tmp_dir/nopivot.txt"
if "$convert_bin" --transform phaser-hash < "$tmp_dir/nopivot.txt" | grep -q '"pivot"'; then
  echo "phaser frames without a pivot marker must not carry a pivot" >&2
  exit 1
fi
printf 'path "a.png"\n- marker "pivot" point 4,8\n' > "$tmp_dir/nopivot.markers"
"$convert_bin" --transform phaser-array --markers "$(fix_path "$tmp_dir/nopivot.markers")" < "$tmp_dir/nopivot.txt" \
  | tr -d ' \n' | grep -qF '"pivot":{"x":0.5,"y":1}'

# nine-patch: the 1px marker border is not part of the exported image
printf 'atlas 16,16\nsprite "padded.9.png" 0,0 8,8 4,3 4,3 slice=4,4,2,2\n' > "$tmp_dir/ninepatch.txt"
"$convert_bin" --transform plist < "$tmp_dir/ninepatch.txt" > "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{14,12}</string>' "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{{3,2},{8,8}}</string>' "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{{4,4},{8,6}}</string>' "$tmp_dir/out.ninepatch.plist"
printf 'atlas 16,16\nsprite "plain.9.png" 0,0 14,12 1,1 1,1 slice=4,4,2,2\n' \
  | "$convert_bin" --transform phaser-hash | tr -d ' \n' | grep -qF '"sourceSize":{"h":12,"w":14},"spriteSourceSize":{"h":12,"w":14,"x":0,"y":0},"trimmed":false'

# --image-path: the reference written into metadata; -a still names files and seeds GUIDs
printf 'atlas 32,32\nsprite "a.png" 0,0 16,16\natlas 32,32\nsprite "b.png" 0,0 8,8\n' > "$tmp_dir/ip.txt"
"$convert_bin" --transform libgdx -a 'out/atlas_%d.png' < "$tmp_dir/ip.txt" | grep -qx 'out/atlas_1.png'
"$convert_bin" --transform libgdx -a 'out/atlas_%d.png' --image-path 'atlas_%d.png' < "$tmp_dir/ip.txt" > "$tmp_dir/out.ip.atlas"
grep -qx 'atlas_1.png' "$tmp_dir/out.ip.atlas"
if grep -q 'out/' "$tmp_dir/out.ip.atlas"; then
  echo "--image-path must replace the -a path inside the metadata" >&2
  exit 1
fi
"$convert_bin" --transform plist -a 'out/sheet_%d.png' --image-path 'tex/sheet_%d.png' --output-dir "$tmp_dir/ip_plist" < "$tmp_dir/ip.txt"
grep -qF '<string>tex/sheet_1.png</string>' "$tmp_dir/ip_plist/sheet_1.plist"
"$convert_bin" --transform unity.meta -a 'out/sheet_%d.png' --output-dir "$tmp_dir/ip_meta_a" < "$tmp_dir/ip.txt"
"$convert_bin" --transform unity.meta -a 'out/sheet_%d.png' --image-path 'x_%d.png' --output-dir "$tmp_dir/ip_meta_b" < "$tmp_dir/ip.txt"
diff "$tmp_dir/ip_meta_a/sheet_1.png.meta" "$tmp_dir/ip_meta_b/sheet_1.png.meta"
"$convert_bin" --transform plist --image-path 'tex/hero_%d.png' --output-dir "$tmp_dir/ip_only" < "$tmp_dir/ip.txt"
[ -f "$tmp_dir/ip_only/hero_1.plist" ]
if "$convert_bin" --transform json --image-path 'same.png' < "$tmp_dir/ip.txt" > /dev/null 2>&1; then
  echo "--image-path without %d must be refused for multipack layouts" >&2
  exit 1
fi

# Marker values must stay within 2^24 atlas pixels (sprat-lib's SPRAT_COORD_LIMIT).
printf 'atlas 8,8\nsprite "a.png" 0,0 4,4\n' > "$tmp_dir/limit.txt"
printf 'path "a.png"\n- marker "pivot" point 16777216,-16777216\n' > "$tmp_dir/limit_ok.markers"
"$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_ok.markers")" < "$tmp_dir/limit.txt" > /dev/null
printf 'path "a.png"\n- marker "hit" polygon 0,0 16777217,0\n' > "$tmp_dir/limit_bad.markers"
if "$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_bad.markers")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/limit.err"; then
  echo "a marker beyond the coordinate limit should fail" >&2
  exit 1
fi
grep -qF 'Marker "hit" of sprite a.png has a value beyond 16777216 atlas pixels' "$tmp_dir/limit.err"
# The limit applies once scaled: 20000000 at scale 0.5 exports as 10000000.
printf 'scale 0.5\natlas 8,8\nsprite "a.png" 0,0 4,4\n' > "$tmp_dir/limit_scaled.txt"
printf 'path "a.png"\n- marker "pivot" point 20000000,0\n' > "$tmp_dir/limit_scaled.markers"
"$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_scaled.markers")" \
    < "$tmp_dir/limit_scaled.txt" > /dev/null

echo "convert_test.sh: ok"
