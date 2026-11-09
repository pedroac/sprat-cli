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
sprite "./frames/a.png" rect:0,0,16,16
sprite "./frames/b.png" rect:16,0,8,8 trim:1,2,3,4 rotated
LAYOUT

layout_quotes_file="$tmp_dir/layout.quotes.txt"
cat > "$layout_quotes_file" <<'LAYOUTQ'
atlas 8,8
scale 1
sprite "./frames/a\"q.png" rect:0,0,8,8
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

# ── Tiles (--tiles) ──────────────────────────────────────────────────────────
tiles_file="$tmp_dir/tiles.txt"
cat > "$tiles_file" <<'TILES'
flags solid water
tile grass code:G layers:0,2 flags:solid,water
theme default
- sprite "./frames/a.png" rules:isolated
- sprite:b rules:g[2,46]
TILES

"$convert_bin" --transform json --tiles "$(fix_path "$tiles_file")" < "$layout_file" > "$tmp_dir/out.tiles.json"
grep -q '"tiles": \[' "$tmp_dir/out.tiles.json"
grep -q '"tile_flags": \[' "$tmp_dir/out.tiles.json"
grep -q '"name": "solid"' "$tmp_dir/out.tiles.json"
grep -q '"name": "water"' "$tmp_dir/out.tiles.json"
grep -q '"code": "G"' "$tmp_dir/out.tiles.json"
grep -q '"flags_bitmask": 3' "$tmp_dir/out.tiles.json"
grep -q '"sprite_path": "./frames/a.png"' "$tmp_dir/out.tiles.json"
grep -q '"rules": "isolated"' "$tmp_dir/out.tiles.json"
grep -q '"rules": "g\[2,46\]"' "$tmp_dir/out.tiles.json"
# The colon form "- sprite:NAME" is the same line as "- sprite NAME".
grep -q '"sprite_name": "b"' "$tmp_dir/out.tiles.json"

# A duplicate code on one line is reported and the tile is dropped, without breaking the
# JSON output or taking the code away from a later tile.
printf 'tile a code:A code:B\ntile b code:A\n' > "$tmp_dir/tiles.code.txt"
"$convert_bin" --transform json --tiles "$(fix_path "$tmp_dir/tiles.code.txt")" < "$layout_file" \
  > "$tmp_dir/out.tiles.code.json" 2> "$tmp_dir/tiles.code.err"
grep -qF 'Tile "a" has more than one code' "$tmp_dir/tiles.code.err"
grep -q '"code": "A"' "$tmp_dir/out.tiles.code.json"
if grep -q '"code": "B"' "$tmp_dir/out.tiles.code.json"; then
  echo "tile with a duplicate code was kept" >&2
  exit 1
fi
if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$(fix_path "$tmp_dir/out.tiles.code.json")" > /dev/null
fi

# A repeated theme name within a tile is reported and the second theme is dropped.
printf 'tile grass code:G\ntheme default\ntheme default\n' > "$tmp_dir/tiles.theme.txt"
"$convert_bin" --transform json --tiles "$(fix_path "$tmp_dir/tiles.theme.txt")" < "$layout_file" \
  > /dev/null 2> "$tmp_dir/tiles.theme.err"
grep -qF 'Tile "grass" has more than one theme "default"' "$tmp_dir/tiles.theme.err"

# A variation under an ignored theme is reported, not dropped silently.
printf 'tile grass code:G\ntheme default\ntheme default\n- sprite a\n' > "$tmp_dir/tiles.theme.sprite.txt"
"$convert_bin" --transform json --tiles "$(fix_path "$tmp_dir/tiles.theme.sprite.txt")" < "$layout_file" \
  > /dev/null 2> "$tmp_dir/tiles.theme.sprite.err"
grep -qF 'Sprite variation outside a theme block' "$tmp_dir/tiles.theme.sprite.err"

# A repeated variation name within a theme is reported and the second one is dropped.
printf 'tile grass code:G\ntheme default\n- sprite a\n- sprite a\n' > "$tmp_dir/tiles.variation.txt"
"$convert_bin" --transform json --tiles "$(fix_path "$tmp_dir/tiles.variation.txt")" < "$layout_file" \
  > "$tmp_dir/out.tiles.variation.json" 2> "$tmp_dir/tiles.variation.err"
grep -qF 'Tile "grass" theme "default" has more than one variation "a"' "$tmp_dir/tiles.variation.err"
if [ "$(grep -c '"sprite_name": "a"' "$tmp_dir/out.tiles.variation.json")" -ne 1 ]; then
  echo "duplicate variation was kept" >&2
  exit 1
fi

# Tiles lines are read from the layout stream when --tiles is absent.
cat "$layout_file" "$tiles_file" > "$tmp_dir/layout.tiles.txt"
"$convert_bin" --transform json < "$tmp_dir/layout.tiles.txt" > "$tmp_dir/out.tiles.stream.json"
grep -q '"name": "grass"' "$tmp_dir/out.tiles.stream.json"

# The raw transform writes the tile records and counts them on the sheet line.
"$convert_bin" --transform raw --tiles "$(fix_path "$tiles_file")" < "$layout_file" > "$tmp_dir/out.tiles.sprat"
cat > "$tmp_dir/expected.tiles.sprat" <<'RAW'
sprat-raw 11
sheet 1 0 0 1 2 0 0 2 1 1 2 0 0 0 0 0 0 0 0 0 0 0 0 83
atlas 64 32 0:
tile_flag 5:solid
tile_flag 5:water
tile 1:G 2 0 2 1 3 5:grass
theme 7:default 2
variation 14:./frames/a.png 0 8:isolated
variation 1:b 1 7:g[2,46]
sprite 0 0 0 16 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 0 1:a 14:./frames/a.png
sprite 0 16 0 8 8 90 1 2 3 4 0 0 0 0 0 0 0 0 0 0 -1 0 1:b 14:./frames/b.png
RAW
if ! diff "$tmp_dir/expected.tiles.sprat" "$tmp_dir/out.tiles.sprat"; then
  echo "raw transform tiles mismatch" >&2
  exit 1
fi

"$convert_bin" --help | grep -q -- '--tiles'

# ── Raw transform (sprat-lib's input) ────────────────────────────────────────
raw_layout_file="$tmp_dir/raw.layout.txt"
cat > "$raw_layout_file" <<'LAYOUT'
atlas 64,32
scale 1
sprite "./frames/a.png" rect:0,0,16,16
sprite "./frames/b.png" rect:16,0,8,8 trim:1,2,3,4 rotated
sprite "./frames/c dé.png" rect:24,0,8,8 slice:1,2,3,4,repeat,mirror
alias "./frames/a.png" -> "./frames/d.png"
LAYOUT
raw_markers_file="$tmp_dir/raw.markers.txt"
cat > "$raw_markers_file" <<'MARKERS'
- marker "spawn" point 1,1
path "./frames/a.png"
- marker "hit" point 3,5
- marker "hurt" circle 6,7 4
- marker "zone" polygon 0,0 4,0 2,3
path "b"
- marker "foot" rectangle 1,2 3,4
- marker "pivot" point 0,0
MARKERS
raw_animations_file="$tmp_dir/raw.animations.txt"
cat > "$raw_animations_file" <<'ANIMS'
animation "run" 8 once-pingpong
- frame "./frames/a.png"
- frame "b"
animation "run-left" alias "run" flip hv
ANIMS
# String lengths count UTF-8 bytes ("c dé" is 5); an alias points at its source by index
# and plays as it does (LOOP 3 is once-pingpong). The global marker comes before the
# sprites; b's pivot at 0,0 still sets HAS_PIVOT; the deduplicated d names a by index.
# The sheet line totals no puppets, tile flags, tiles, themes or variations, 1 global marker,
# 6 markers, 3 vertices, 2 frames, no parts, variants, joints, poses, settings, sequences,
# steps or part eases, and 104 string bytes: spawn 5, sprites 1+14 + 1+14 + 5+18 +
# 1+14, markers 3+4+4+4+5, animations 3+8.
cat > "$tmp_dir/expected.sprat" <<'RAW'
sprat-raw 11
sheet 1 0 0 1 4 2 0 0 0 0 0 1 6 3 2 0 0 0 0 0 0 0 0 104
atlas 64 32 0:
marker 0 1 1 0 0 0 0 5:spawn
sprite 0 0 0 16 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 3 1:a 14:./frames/a.png
marker 0 3 5 0 0 0 0 3:hit
marker 1 6 7 4 0 0 0 4:hurt
marker 3 0 0 0 0 0 3 0 0 4 0 2 3 4:zone
sprite 0 16 0 8 8 90 1 2 3 4 1 0 0 0 0 0 0 0 0 0 -1 2 1:b 14:./frames/b.png
marker 2 1 2 0 3 4 0 4:foot
marker 0 0 0 0 0 0 0 5:pivot
sprite 0 24 0 8 8 0 0 0 0 0 0 0 0 1 1 2 3 4 1 2 -1 0 5:c dé 18:./frames/c dé.png
sprite 0 0 0 16 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:d 14:./frames/d.png
anim 8 0 3 -1 2 0 1 3:run
anim 8 3 3 0 0 8:run-left
RAW
"$convert_bin" --transform raw --markers "$(fix_path "$raw_markers_file")" \
  --animations "$(fix_path "$raw_animations_file")" < "$raw_layout_file" > "$tmp_dir/out.sprat"
if ! diff "$tmp_dir/expected.sprat" "$tmp_dir/out.sprat"; then
  echo "raw transform output mismatch" >&2
  exit 1
fi
# Puppets: parts in draw order with their variants (a sprite by path or name, an animation,
# alias included), the joints, then the poses. A part that isn't attached has a position;
# the arm's second attach, and the positioned fly's, are constraints. Joint markers are
# checked in every sprite: a has no pivot, the deduplicated d has no markers, b has no "hit"
# or "zone", and the fly's constraint needs a "zone" in the arm's frames. A pose sets parts in
# part order, and positions and offsets are scaled; scales, opacities and tints aren't. A
# setting without its flag has its rest value: scale 1, opacity 1, a white tint.
raw_puppets_file="$tmp_dir/raw.puppets.txt"
cat > "$raw_puppets_file" <<'PUPPETS'
root "./frames"
puppet "hé"
part "body" at:0,-20
- sprite "a.png"
- sprite d -> "body-2"
part "arm"
- animation "run-left" -> "swing"
part "fly" at:-6,-30
- sprite b
attach "arm":"pivot" to "body":zone leeway:2 angle:-30..45
attach "arm":"hit" to "body":"zone" leeway:5
attach "fly":hit to "arm":zone
pose "wave"
- part "arm" angle:-30 variant:"swing" offset:1,-2 scale:1.5,-0.25
- part body variant:body-2 tint:#FF8000
pose "buzz"
- part "fly" at:-4,-28 angle:90 opacity:0.5
sequence "dance" fps:12 loop:once
- pose "wave" in:2 hold:3 step:"arm"
- pose buzz hold:1 ease:step linear:fly
PUPPETS
# 1 puppet, no tile flags, tiles, themes or variations, 3 parts, 4 variants, 3 joints,
# 2 poses, 3 settings, 1 sequence, 2 steps and 2 part eases (the arm snaps in a linear
# step, the fly tweens in a snapping one);
# strings add hé 3, body 4 + a 1 + body-2 6, arm 3 + swing 5, fly 3 + b 1, the markers
# pivot 5 + zone 4, hit 3 + zone 4 and hit 3 + zone 4, the poses wave 4 and buzz 4, and the
# sequence dance 5 to the 104 bytes above.
cat > "$tmp_dir/expected.puppets.sprat" <<'RAW'
sheet 1 0 0 1 4 2 1 0 0 0 0 1 6 3 2 3 4 3 2 3 1 2 2 166
puppet 3 3 2 1 3:hé
part -1 1 0 -20 2 4:body
variant 0 0 1:a
variant 0 3 6:body-2
part 0 0 0 0 1 3:arm
variant 1 1 5:swing
part -1 1 -6 -30 1 3:fly
variant 0 1 1:b
joint 1 0 0 2 1 -30 45 5:pivot 4:zone
joint 1 0 1 5 0 0 0 3:hit 4:zone
joint 2 1 1 0 0 0 0 3:hit 4:zone
pose 2 4:wave
set 0 1 0 0 0 0 0 0 0 0 0 1 1 0 1 1 255 128 0
set 1 0 1 -30 1 1 -2 0 0 0 1 1.5 -0.25 0 1 0 255 255 255
pose 1 4:buzz
set 2 -1 1 90 0 0 0 1 -4 -28 0 1 1 1 0.5 0 255 255 255
sequence 12 1 2 5:dance
step 0 2 3 0 1 1 1
step 1 0 1 1 1 2 0
RAW
"$convert_bin" --transform raw --markers "$(fix_path "$raw_markers_file")" \
  --animations "$(fix_path "$raw_animations_file")" --puppets "$(fix_path "$raw_puppets_file")" \
  < "$raw_layout_file" > "$tmp_dir/out.puppets.sprat" 2> "$tmp_dir/puppets.err"
{ sed -n 2p "$tmp_dir/out.puppets.sprat"; sed -n '/^puppet /,$p' "$tmp_dir/out.puppets.sprat"; } \
  > "$tmp_dir/out.puppets.tail"
if ! diff "$tmp_dir/expected.puppets.sprat" "$tmp_dir/out.puppets.tail"; then
  echo "raw transform puppets mismatch" >&2
  exit 1
fi
grep -qF 'Puppet "hé" part "arm" sprite "a" has no point marker "pivot" to attach it by' "$tmp_dir/puppets.err"
grep -qF 'Puppet "hé" part "body" sprite "d" has no marker "zone" to attach part "arm" to' "$tmp_dir/puppets.err"
grep -qF 'Puppet "hé" part "arm" sprite "b" has no marker "zone" to attach part "fly" to' "$tmp_dir/puppets.err"
grep -qF 'Puppet "hé" part "arm" sprite "b" has no point marker "hit" to attach it by' "$tmp_dir/puppets.err"
grep -qF 'Puppet "hé" part "fly" sprite "b" has no point marker "hit" to attach it by' "$tmp_dir/puppets.err"
# body's d lacks the zone for both of the arm's attaches; that is said once.
if [ "$(grep -c 'Warning' "$tmp_dir/puppets.err")" -ne 5 ]; then
  echo "unexpected puppets warnings:" >&2
  cat "$tmp_dir/puppets.err" >&2
  exit 1
fi
# The same lines are read from the layout stream without --puppets, and positions and
# leeways are scaled to atlas pixels like markers.
printf 'local s = std.extVar("sprat");\n{ extension: ".json", content: std.manifestJson(s.puppets) }\n' \
  > "$tmp_dir/puppets.jsonnet"
sed 's/^scale 1$/scale 0.5/' "$raw_layout_file" | cat - "$raw_animations_file" "$raw_puppets_file" \
  | "$convert_bin" --transform "$(fix_path "$tmp_dir/puppets.jsonnet")" 2> /dev/null \
  | python3 -c '
import json, sys
p = json.load(sys.stdin.buffer)  # bytes: Windows would decode text with its ANSI code page
assert [x["name"] for x in p] == ["hé"], p
parts, joint = p[0]["parts"], p[0]["joints"][0]
assert [(x["name"], x["parent"], x["has_position"], x["x"], x["y"]) for x in parts] \
    == [("body", -1, True, 0, -10), ("arm", 0, False, 0, 0), ("fly", -1, True, -3, -15)], parts
assert [j["is_constraint"] for j in p[0]["joints"]] == [False, True, True], p[0]["joints"]
wave, buzz = p[0]["poses"]
assert [(s["part_name"], s["variant_name"], s["angle"], s["offset_x"], s["offset_y"]) for s in wave["parts"]] \
    == [("body", "body-2", 0, 0, 0), ("arm", "swing", -30, 1, -1)], wave
assert (buzz["parts"][0]["x"], buzz["parts"][0]["y"]) == (-2, -14), buzz
assert [(s["has_scale"], s["scale_x"], s["scale_y"]) for s in wave["parts"]] \
    == [(False, 1, 1), (True, 1.5, -0.25)], wave
assert [(s["has_tint"], s["tint_r"], s["tint_g"], s["tint_b"]) for s in wave["parts"]] \
    == [(True, 255, 128, 0), (False, 255, 255, 255)], wave
assert (buzz["parts"][0]["has_opacity"], buzz["parts"][0]["opacity"]) == (True, 0.5), buzz
dance = p[0]["sequences"][0]
assert (dance["name"], dance["fps"], dance["loop"], dance["frame_count"], dance["duration"]) \
    == ("dance", 12, "once", 6, 0.5), dance
assert [(t["pose_name"], t["in"], t["hold"], t["ease"]) for t in dance["steps"]] \
    == [("wave", 2, 3, "linear"), ("buzz", 0, 1, "step")], dance
assert [[(e["part"], e["part_name"], e["ease"]) for e in t["part_eases"]] for t in dance["steps"]] \
    == [[(1, "arm", "step")], [(2, "fly", "linear")]], dance
assert [(v["name"], v["kind"], v["sprite_index"], v["animation_index"]) for v in parts[1]["variants"]] \
    == [("swing", "animation", -1, 1)], parts
assert (joint["child_name"], joint["parent_name"], joint["leeway"], joint["angle_min"], joint["angle_max"]) \
    == ("arm", "body", 1, -30, 45), joint
'

# Mistakes in a puppets file: structural ones fail, the rest are reported and skipped.
puppet_fails() {
  printf '%s\n' "$@" > "$tmp_dir/bad.puppets.txt"
  if "$convert_bin" --puppets "$(fix_path "$tmp_dir/bad.puppets.txt")" < "$raw_layout_file" \
      > /dev/null 2> "$tmp_dir/bad.puppets.err"; then
    echo "puppets file accepted: $*" >&2
    exit 1
  fi
}
puppet_fails 'puppet "p"' 'part "x"' 'attach "x":a to "y":b'
grep -qF 'Puppet "p" attaches unknown part "y"' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x"' 'attach "x":a to "x":b'
grep -qF 'Puppet "p" attaches part "x" to itself' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x"' 'part "y"' 'attach x:a to y:b' 'attach y:a to x:b'
grep -qF 'is part of a cycle of attachments' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x"' 'part "x"'
grep -qF 'Puppet "p" has more than one part "x"' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x"' '- sprite a' '- sprite "./frames/a.png"'
grep -qF 'Puppet "p" part "x" has more than one variant "a"' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'puppet "p"'
grep -qF 'Puppet "p" is defined more than once' "$tmp_dir/bad.puppets.err"
# An empty name would be written to raw as "none", so it is never a name.
puppet_fails 'puppet ""'
grep -qF 'Puppets file line "puppet """ is not puppet "NAME"' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x"' 'part "y" at:0,0' '- sprite a' 'attach "x":a to "y":b leeway:99999999'
grep -qF 'Puppet "p" part "x" has a leeway beyond 16777216 atlas pixels' "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x" at:99999999,0'
grep -qF 'Puppet "p" part "x" has a position beyond 16777216 atlas pixels' "$tmp_dir/bad.puppets.err"
# Each part is placed one way: by its position or by its parent joint; of two attached
# parts, one is placed by the joint.
puppet_fails 'puppet "p"' 'part "x" at:1,1' 'part "y" at:0,0' 'attach x:a to y:b'
grep -qF 'Puppet "p" attaches "x" to "y", and both have a position; one of them must be placed by the joint' \
  "$tmp_dir/bad.puppets.err"
puppet_fails 'puppet "p"' 'part "x" at:0,0' 'part "y"'
grep -qF 'Puppet "p" part "y" has no position and is attached to no part; give it at:X,Y or attach it' \
  "$tmp_dir/bad.puppets.err"
# Poses set known parts and variants, each part once, in the way the rig allows.
pose_fails() {
  puppet_fails 'puppet "p"' 'part "x" at:0,0' '- sprite a' 'part "y"' '- sprite b' \
    'attach y:a to x:b angle:-10..10' 'pose "q"' "$@"
}
pose_fails '- part "z" angle:1'
grep -qF 'Puppet "p" pose "q" sets unknown part "z"' "$tmp_dir/bad.puppets.err"
pose_fails '- part y angle:1' '- part y angle:2'
grep -qF 'Puppet "p" pose "q" sets part "y" more than once' "$tmp_dir/bad.puppets.err"
pose_fails '- part y variant:c'
grep -qF 'Puppet "p" pose "q" part "y" has no variant "c"' "$tmp_dir/bad.puppets.err"
pose_fails '- part y at:1,1'
grep -qF 'Puppet "p" pose "q" moves part "y", which its joint places' "$tmp_dir/bad.puppets.err"
pose_fails '- part x offset:1,1'
grep -qF 'Puppet "p" pose "q" offsets part "x", which has no joint to drift in' "$tmp_dir/bad.puppets.err"
pose_fails '- part y angle:11'
grep -qF 'Puppet "p" pose "q" turns part "y" 11 degrees, outside its joint'"'"'s -10..10' "$tmp_dir/bad.puppets.err"
pose_fails '- part x angle:1' 'pose "q"'
grep -qF 'Puppet "p" has more than one pose "q"' "$tmp_dir/bad.puppets.err"
pose_fails '- part x at:99999999,0'
grep -qF 'Puppet "p" pose "q" places part "x" beyond 16777216 atlas pixels' "$tmp_dir/bad.puppets.err"
# Sequences play known poses; their names are unique and fps follows the animations limit.
pose_fails '- part x angle:1' 'sequence "s"' '- pose "nope" hold:1'
grep -qF 'Puppet "p" sequence "s" plays unknown pose "nope"' "$tmp_dir/bad.puppets.err"
pose_fails '- part x angle:1' 'sequence "s"' 'sequence "s"'
grep -qF 'Puppet "p" has more than one sequence "s"' "$tmp_dir/bad.puppets.err"
pose_fails '- part x angle:1' 'sequence "s"' '- pose q hold:1 step:x,"z"'
grep -qF 'Puppet "p" sequence "s" step 1 eases unknown part "z"' "$tmp_dir/bad.puppets.err"
pose_fails '- part x angle:1' 'sequence "s" fps:101'
grep -qF 'Sequence line "sequence "s" fps:101" has fps 101, above the limit of 100' "$tmp_dir/bad.puppets.err"
# Without fps or loop, a sequence plays at 8 fps and loops; its lines are checked one by one.
printf '%s\n' 'puppet "p"' 'part "x" at:0,0' 'pose "q"' '- part x angle:1' \
  'sequence "s" fps:0 loop:twice speed:2' '- pose q in:-1 hold:100001 ease:smooth' '- pose q' \
  '- pose q in:1 step:x,"x" linear:x ease:linear' '- pose q in:1 ease:step step:x' '- pose q in:1 step:w' \
  '- pose' 'sequence "empty"' 'sequence ""' 'part "w" at:1,1' '- pose q hold:1' \
  > "$tmp_dir/sequence.puppets.txt"
"$convert_bin" --transform raw --puppets "$(fix_path "$tmp_dir/sequence.puppets.txt")" < "$raw_layout_file" \
  > "$tmp_dir/sequence.sprat" 2> "$tmp_dir/sequence.err"
grep -qF 'sequence 8 0 3 1:s' "$tmp_dir/sequence.sprat"
grep -qF 'step 0 1 0 0 1 0 1' "$tmp_dir/sequence.sprat"
grep -qF 'step 0 1 0 1 0' "$tmp_dir/sequence.sprat"
grep -qF 'Step line "- pose q in:1 step:x,"x" linear:x ease:linear" eases part "x" more than once; the first is kept' "$tmp_dir/sequence.err"
grep -qF 'Step line "- pose q in:1 ease:step step:x" eases part "x" step, as the whole step does; it is ignored' "$tmp_dir/sequence.err"
grep -qF 'Puppet "p" sequence "s" step 3 eases part "w", which pose "q" doesn'"'"'t set; it is ignored' "$tmp_dir/sequence.err"
grep -qF 'sequence 8 0 0 5:empty' "$tmp_dir/sequence.sprat"
# A uniform scale sets both axes; values out of range or malformed are reported and ignored,
# and a pose line left with nothing to set is ignored too.
printf '%s\n' 'puppet "p"' 'part "x" at:0,0' 'pose "q"' '- part x scale:-2 opacity:0 tint:#00ff7F' \
  'pose "r"' '- part x scale:17 opacity:1.5 tint:#-1ffff' '- part x scale:1,a tint:fff' \
  > "$tmp_dir/look.puppets.txt"
"$convert_bin" --transform raw --puppets "$(fix_path "$tmp_dir/look.puppets.txt")" < "$raw_layout_file" \
  > "$tmp_dir/look.sprat" 2> "$tmp_dir/look.err"
grep -qxF 'set 0 -1 0 0 0 0 0 0 0 0 1 -2 -2 1 0 1 0 255 127' "$tmp_dir/look.sprat"
grep -qF 'Pose line "- part x scale:17 opacity:1.5 tint:#-1ffff" scale "17" is not a factor, or X,Y factors, between -16 and 16; it is ignored' "$tmp_dir/look.err"
grep -qF 'opacity "1.5" is not between 0 and 1; it is ignored' "$tmp_dir/look.err"
grep -qF 'tint "#-1ffff" is not #RRGGBB; it is ignored' "$tmp_dir/look.err"
grep -qF 'scale "1,a" is not a factor' "$tmp_dir/look.err"
grep -qF 'Pose line "- part x scale:1,a tint:fff" sets nothing; it is ignored' "$tmp_dir/look.err"
grep -qF 'fps "0" is not a whole number of frames per second, 1 or more' "$tmp_dir/sequence.err"
grep -qF 'loop "twice" is not one of loop, once, pingpong, once-pingpong' "$tmp_dir/sequence.err"
grep -qF 'has an unknown or repeated setting "speed:2"' "$tmp_dir/sequence.err"
grep -qF 'in "-1" is not a whole number of frames from 0 to 100000' "$tmp_dir/sequence.err"
grep -qF 'hold "100001" is not a whole number of frames from 0 to 100000' "$tmp_dir/sequence.err"
grep -qF 'has an unknown or repeated setting "ease:smooth"' "$tmp_dir/sequence.err"
grep -qF 'Step line "- pose q" lasts no frame' "$tmp_dir/sequence.err"
grep -qF 'Puppets file line "- pose" is not - pose "POSE"' "$tmp_dir/sequence.err"
grep -qF 'Puppet "p" sequence "empty" has no steps' "$tmp_dir/sequence.err"
grep -qF 'Puppets file line "sequence """ is not sequence "NAME"' "$tmp_dir/sequence.err"
grep -qF 'Puppets file line "- pose q hold:1" is not inside a sequence' "$tmp_dir/sequence.err"
printf '%s\n' 'puppet "p"' 'part "x" at:0,0' 'pose "q"' '- part x angle:1' 'sequence "s" fps:12 loop:"ping' \
  '- pose q hold:1' > "$tmp_dir/quote.puppets.txt"
"$convert_bin" --transform raw --puppets "$(fix_path "$tmp_dir/quote.puppets.txt")" < "$raw_layout_file" \
  > "$tmp_dir/quote.sprat" 2> "$tmp_dir/quote.err"
grep -qF 'Sequence line "sequence "s" fps:12 loop:"ping" has a broken quote' "$tmp_dir/quote.err"
grep -qF 'sequence 12 0 1 1:s' "$tmp_dir/quote.sprat"

# A positioned part may turn freely, and a pose's lines are checked one by one.
printf '%s\n' 'puppet "p"' 'part "x" at:0,0' 'pose "q"' '- part x angle:360 variant:"" spin:1 at:1' \
  '- part x' 'pose ""' '- part x angle:1' 'pose "empty"' 'part "w" at:1,1' '- part w angle:1' \
  > "$tmp_dir/pose.puppets.txt"
"$convert_bin" --transform raw --puppets "$(fix_path "$tmp_dir/pose.puppets.txt")" < "$raw_layout_file" \
  > "$tmp_dir/pose.sprat" 2> "$tmp_dir/pose.err"
grep -qF 'set 0 -1 1 360 0 0 0 0 0 0' "$tmp_dir/pose.sprat"
grep -qF 'Pose line "- part x angle:360 variant:"" spin:1 at:1" variant "" is not a variant name' "$tmp_dir/pose.err"
grep -qF 'has an unknown or repeated setting "spin:1"' "$tmp_dir/pose.err"
grep -qF 'at "1" is not X,Y in whole pixels' "$tmp_dir/pose.err"
grep -qF 'Pose line "- part x" sets nothing' "$tmp_dir/pose.err"
grep -qF 'Puppets file line "pose """ is not pose "NAME"; the pose is ignored' "$tmp_dir/pose.err"
grep -qF 'Puppets file line "- part x angle:1" is not inside a pose' "$tmp_dir/pose.err"
grep -qF 'Puppet "p" pose "empty" sets no part' "$tmp_dir/pose.err"
grep -qF 'Puppets file line "- part w angle:1" is not inside a pose' "$tmp_dir/pose.err"
printf '%s\n' 'part "early"' 'puppet "p"' 'part "x"' '- sprite nope' '- sprite 9' '- animation "nope"' \
  '- sprite a => b' 'part "y" at:0,0' 'attach x: a to y:b' 'attach x:a to y:b leeway:-1 angle:5..1 spin:2' \
  'bogus' 'part "z" at:1,x at:5,5 at:6,6 high' '- sprite a -> ""' 'part ""' 'attach z:"" to y:b' \
  > "$tmp_dir/warn.puppets.txt"
"$convert_bin" --puppets "$(fix_path "$tmp_dir/warn.puppets.txt")" < "$raw_layout_file" \
  > /dev/null 2> "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "part "early"" is not inside a puppet' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file sprite "nope" matches no sprite' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file sprite 9 is out of range (the layout has 4 sprites)' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file animation "nope" matches no animation' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "- sprite a => b" is not - sprite' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "attach x: a to y:b" is not attach' "$tmp_dir/warn.puppets.err"
grep -qF 'leeway "-1" is not a whole number of pixels' "$tmp_dir/warn.puppets.err"
grep -qF 'angle "5..1" is not MIN..MAX degrees' "$tmp_dir/warn.puppets.err"
grep -qF 'has an unknown or repeated setting "spin:2"' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "bogus" is not a root, puppet, part' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppet "p" part "x" has no sprites or animations' "$tmp_dir/warn.puppets.err"
grep -qF 'position "1,x" is not at:X,Y in whole pixels' "$tmp_dir/warn.puppets.err"
grep -qF 'has an unknown or repeated setting "at:6,6"' "$tmp_dir/warn.puppets.err"
grep -qF 'has an unknown or repeated setting "high"' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "part """ is not part "NAME" [at:X,Y]' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "- sprite a -> """ is not - sprite' "$tmp_dir/warn.puppets.err"
grep -qF 'Puppets file line "attach z:"" to y:b" is not attach' "$tmp_dir/warn.puppets.err"
printf '%s\n' '- marker "" point 1,1' 'animation ""' > "$tmp_dir/empty.names.txt"
"$convert_bin" --markers "$(fix_path "$tmp_dir/empty.names.txt")" \
  --animations "$(fix_path "$tmp_dir/empty.names.txt")" < "$raw_layout_file" > /dev/null 2> "$tmp_dir/empty.names.err"
grep -qF 'Marker line "- marker "" point 1,1" has no valid name' "$tmp_dir/empty.names.err"
grep -qF 'Animation line "animation """ has no valid name' "$tmp_dir/empty.names.err"

# A root resolves paths but doesn't change sprite names: "b" is still sprite b.
printf 'root "./elsewhere"\npath "b"\n- marker "hit" point 1,1\n' > "$tmp_dir/root.markers.txt"
printf 'root "./elsewhere"\nanimation "x"\n- frame b a\n' > "$tmp_dir/root.animations.txt"
"$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/root.markers.txt")" \
  --animations "$(fix_path "$tmp_dir/root.animations.txt")" < "$raw_layout_file" \
  > "$tmp_dir/root.sprat" 2> "$tmp_dir/root.err"
if [ -s "$tmp_dir/root.err" ]; then
  cat "$tmp_dir/root.err" >&2
  exit 1
fi
grep -qF 'marker 0 1 1 0 0 0 0 3:hit' "$tmp_dir/root.sprat"
grep -qF 'anim 8 0 0 -1 2 1 0 1:x' "$tmp_dir/root.sprat"

# JSON lists the global markers at the top level.
"$convert_bin" --transform json --markers "$(fix_path "$raw_markers_file")" < "$raw_layout_file" \
  | python3 -c 'import json, sys; m = json.load(sys.stdin)["markers"]; assert [x["name"] for x in m] == ["spawn"], m'

# An alias of an unknown animation, or a cycle of aliases, fails whatever the transform.
printf 'animation "x" alias "nope"\n' > "$tmp_dir/raw.bad_alias.txt"
printf 'animation "x" alias "y"\nanimation "y" alias "x"\n' > "$tmp_dir/raw.alias_cycle.txt"
for transform in raw json; do
  if "$convert_bin" --transform "$transform" --animations "$(fix_path "$tmp_dir/raw.bad_alias.txt")" \
      < "$raw_layout_file" > /dev/null 2> "$tmp_dir/raw.bad_alias.err"; then
    echo "$transform transform accepted an alias of an unknown animation" >&2
    exit 1
  fi
  grep -qF 'Animation "x" is an alias of unknown animation "nope"' "$tmp_dir/raw.bad_alias.err"
  if "$convert_bin" --transform "$transform" --animations "$(fix_path "$tmp_dir/raw.alias_cycle.txt")" \
      < "$raw_layout_file" > /dev/null 2> "$tmp_dir/raw.bad_alias.err"; then
    echo "$transform transform accepted a cycle of aliases" >&2
    exit 1
  fi
  grep -qF 'is part of a cycle of aliases' "$tmp_dir/raw.bad_alias.err"
done

# Normal and collision map atlases are no longer supported: their options are rejected
# and no transform mentions them.
for removed in --normal-atlas --collision-atlas --collision-scale; do
  if "$convert_bin" "$removed" x < "$raw_layout_file" > /dev/null 2>&1; then
    echo "spratconvert accepted the removed option $removed" >&2
    exit 1
  fi
done
for t in json godot unity.json raw; do
  "$convert_bin" --transform "$t" < "$raw_layout_file" > "$tmp_dir/no_secondary.out"
  if grep -qiE 'normal_|collision' "$tmp_dir/no_secondary.out"; then
    echo "$t output still mentions normal or collision maps" >&2
    exit 1
  fi
done

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
sprite "f0.png" rect:0,0,16,16
sprite "f1.png" rect:16,0,16,16
sprite "f2.png" rect:32,0,16,16
sprite "f3.png" rect:48,0,16,16
sprite "f4.png" rect:64,0,16,16
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
sprite "p0.png" rect:0,0,16,16
atlas 32,32
sprite "p1.png" rect:0,0,16,16
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
sprite "a.png" rect:0,0,4,4
sprite "b.png" rect:4,0,4,4
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
sprite "idle_10.png" rect:0,0,4,4 trim:1,6,2,0
sprite "idle_2.png" rect:4,0,4,4
alias "idle_10.png" -> "idle_1.png"
alias "idle_10.png" -> "idle_3.png" trim:1,0,2,6
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
printf 'atlas 4,4\nsprite "a.png" rect:0,0,4,4\nalias "missing.png" -> "b.png"\n' > "$tmp_dir/alias_missing.txt"
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
sprite "panel.png" rect:0,0,32,32 slice:8,8,8,8
sprite "border.png" rect:32,0,16,16 slice:4,4,4,4,repeat,stretch
sprite "plain.png" rect:0,32,16,16
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
sprite "button.9.png" rect:0,0,30,30 slice:4,4,4,4
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
sprite "rot.png" rect:0,0,30,50 rotated
atlas 10,10
sprite "plain.png" rect:0,0,10,10
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
printf 'rotation ccw\natlas 4,4\nsprite "a.png" rect:0,0,4,4\n' | "$convert_bin" --transform phaser-hash > /dev/null
"$convert_bin" --transform json < "$tmp_dir/libgdx_multi.txt" | grep -q '"rotation": 270'
"$convert_bin" --transform csv < "$tmp_dir/libgdx_multi.txt" | grep -q ',270,'
"$convert_bin" --transform xml < "$tmp_dir/libgdx_multi.txt" | grep -q 'rotation="270"'
sed '/^rotation ccw$/d' "$tmp_dir/libgdx_multi.txt" | "$convert_bin" --transform json | grep -q '"rotation": 90'
"$convert_bin" --transform css < "$tmp_dir/libgdx_multi.txt" | grep -q 'rotate(90deg) translate(0, -100%)'

# css: quotes in the atlas path and comment terminators in paths are escaped
printf 'atlas 4,4\nsprite "a*/b.png" rect:0,0,4,4\n' > "$tmp_dir/css_escape.txt"
"$convert_bin" --transform css -a "it's.png" < "$tmp_dir/css_escape.txt" > "$tmp_dir/out.escape.css"
grep -qF "url('it\\'s.png')" "$tmp_dir/out.escape.css"
if grep -qF 'a*/b.png' "$tmp_dir/out.escape.css"; then
  echo "css output must not contain a raw comment terminator" >&2
  exit 1
fi

# csv: markers of every type export, and the markers_json column stays one quoted field
printf 'atlas 16,16\nsprite "a.png" rect:0,0,16,16\n' > "$tmp_dir/csv_markers.txt"
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
printf 'atlas 64,32\nsprite "b.png" rect:16,0,10,6 trim:1,2,3,4 rotated\n' > "$tmp_dir/tp_rotated.txt"
for t in phaser-hash phaser-array unity.json aseprite; do
  "$convert_bin" --transform "$t" < "$tmp_dir/tp_rotated.txt" | tr -d ' \n' > "$tmp_dir/out.tp.$t"
  grep -qF '"frame":{"h":10,"w":6,"x":16,"y":0}' "$tmp_dir/out.tp.$t"
done
"$convert_bin" --transform plist < "$tmp_dir/tp_rotated.txt" > "$tmp_dir/out.tp.plist"
grep -qF '<string>{{16,0},{6,10}}</string>' "$tmp_dir/out.tp.plist"
grep -qF '<string>{{1,2},{6,10}}</string>' "$tmp_dir/out.tp.plist"
grep -qF '<string>{-1,1}</string>' "$tmp_dir/out.tp.plist"
# odd trim differences give a half-pixel offset instead of rounding it away
printf 'atlas 8,8\nsprite "c.png" rect:0,0,4,4 trim:1,0,0,0\n' | "$convert_bin" --transform plist | grep -qF '<string>{0.5,0}</string>'

# godot: animations list their frames in play order; from/to only for one ascending run
printf 'atlas 24,8\nsprite "0.png" rect:0,0,8,8\nsprite "1.png" rect:8,0,8,8\nsprite "2.png" rect:16,0,8,8\n' > "$tmp_dir/godot_anim.txt"
printf 'animation "back"\n- frame 2\n- frame 0\nanimation "run"\n- frame 0\n- frame 1\n' > "$tmp_dir/godot_anim.anims"
"$convert_bin" --transform godot --animations "$(fix_path "$tmp_dir/godot_anim.anims")" < "$tmp_dir/godot_anim.txt" | tr -d ' \n' > "$tmp_dir/out.godot_anim"
grep -qF '{"frames":[2,0],"loop":true,"name":"back","speed":8}' "$tmp_dir/out.godot_anim"
grep -qF '{"frames":[0,1],"from":0,"loop":true,"name":"run","speed":8,"to":1}' "$tmp_dir/out.godot_anim"

# loop modes: the "loop" line sets the default, a mode after an animation's name (before
# or after its fps) sets its own, and aliases play the way their source does
cat > "$tmp_dir/loop.anims" <<'LOOPANIMS'
loop once
animation "dflt"
- frame 0
- frame 1
animation "fwd" loop
- frame 0
- frame 1
animation "pp" 12 pingpong
- frame 0
- frame 1
- frame 2
animation "opp" once-pingpong 6
- frame 0
- frame 1
- frame 2
animation "pp-left" alias "pp" flip h
LOOPANIMS
loop_anims="$(fix_path "$tmp_dir/loop.anims")"
"$convert_bin" --transform json --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" > "$tmp_dir/out.loop.json"
python3 - "$tmp_dir/out.loop.json" <<'PY'
import json, sys
anims = {a['name']: a for a in json.load(open(sys.argv[1]))['animations']}
assert anims['dflt']['loop'] == 'once', anims['dflt']
assert anims['fwd']['loop'] == 'loop', anims['fwd']
assert anims['pp']['loop'] == 'pingpong' and anims['pp']['fps'] == 12, anims['pp']
assert anims['opp']['loop'] == 'once-pingpong' and anims['opp']['fps'] == 6, anims['opp']
assert 'loop' not in anims['pp-left'], anims['pp-left']
PY
# a custom transform sees an alias's mode resolved from its source
printf 'local s = std.extVar("sprat");\n{ name: "loop", extension: ".txt", content: std.join(",", [a.name + "=" + a.loop for a in s.animations]) }\n' > "$tmp_dir/loop.jsonnet"
"$convert_bin" --transform "$(fix_path "$tmp_dir/loop.jsonnet")" --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" \
  | grep -qxF 'dflt=once,fwd=loop,pp=pingpong,opp=once-pingpong,pp-left=pingpong'
# godot and unity have only a loop flag, so a ping-pong lists its way back
"$convert_bin" --transform godot --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" | tr -d ' \n' > "$tmp_dir/out.loop.godot"
grep -qF '{"frames":[0,1],"from":0,"loop":false,"name":"dflt","speed":8,"to":1}' "$tmp_dir/out.loop.godot"
grep -qF '{"frames":[0,1,2,1],"loop":true,"name":"pp","speed":12}' "$tmp_dir/out.loop.godot"
grep -qF '{"frames":[0,1,2,1,0],"loop":false,"name":"opp","speed":6}' "$tmp_dir/out.loop.godot"
"$convert_bin" --transform unity.anim --animations "$loop_anims" --output-dir "$tmp_dir/loop_unity" < "$tmp_dir/godot_anim.txt"
grep -qF 'm_LoopTime: 0' "$tmp_dir/loop_unity/dflt.anim"
grep -qF 'm_LoopTime: 1' "$tmp_dir/loop_unity/fwd.anim"
test "$(grep -c -- '- time:' "$tmp_dir/loop_unity/pp.anim")" -eq 4
grep -qF 'm_StopTime: 0.33333333' "$tmp_dir/loop_unity/pp.anim"
grep -qF 'm_LoopTime: 1' "$tmp_dir/loop_unity/pp.anim"
test "$(grep -c -- '- time:' "$tmp_dir/loop_unity/opp.anim")" -eq 5
grep -qF 'm_LoopTime: 0' "$tmp_dir/loop_unity/opp.anim"
# phaser and aseprite play ping-pong themselves
"$convert_bin" --transform phaser-anims --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" > "$tmp_dir/out.loop.phaser"
"$convert_bin" --transform aseprite --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" > "$tmp_dir/out.loop.aseprite"
python3 - "$tmp_dir/out.loop.phaser" "$tmp_dir/out.loop.aseprite" <<'PY'
import json, sys
anims = {a['key']: a for a in json.load(open(sys.argv[1]))['anims']}
assert anims['dflt']['repeat'] == 0 and 'yoyo' not in anims['dflt'], anims['dflt']
assert anims['fwd']['repeat'] == -1 and 'yoyo' not in anims['fwd'], anims['fwd']
assert anims['pp']['repeat'] == -1 and anims['pp']['yoyo'] is True, anims['pp']
assert anims['opp']['repeat'] == 0 and anims['opp']['yoyo'] is True, anims['opp']
assert len(anims['pp']['frames']) == 3, anims['pp']
tags = {t['name']: t for t in json.load(open(sys.argv[2]))['meta']['frameTags']}
assert tags['dflt']['direction'] == 'forward' and tags['dflt']['repeat'] == '1', tags['dflt']
assert tags['fwd']['direction'] == 'forward' and 'repeat' not in tags['fwd'], tags['fwd']
assert tags['pp']['direction'] == 'pingpong' and 'repeat' not in tags['pp'], tags['pp']
assert tags['opp']['direction'] == 'pingpong' and tags['opp']['repeat'] == '2', tags['opp']
PY
"$convert_bin" --transform xml --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" \
  | grep -qF '<animation index="2" name="pp" fps="12" loop="pingpong" sprite_indexes="0,1,2" />'
"$convert_bin" --transform csv --animations "$loop_anims" < "$tmp_dir/godot_anim.txt" > "$tmp_dir/out.loop.csv"
grep -qxF 'animation,2,pp,12,0|1|2,,pingpong' "$tmp_dir/out.loop.csv"
# unknown modes and extra settings are ignored with a warning
printf 'loop forever\nloop\nanimation "run" 8 bounce\n- frame 0\nanimation "walk" sideways\n- frame 0\n' > "$tmp_dir/loop_bad.anims"
"$convert_bin" --transform json --animations "$(fix_path "$tmp_dir/loop_bad.anims")" \
  < "$tmp_dir/godot_anim.txt" > "$tmp_dir/out.loop_bad.json" 2> "$tmp_dir/loop_bad.err"
grep -qF 'Animations file loop "forever" is not one of loop, once, pingpong, once-pingpong; it is ignored' "$tmp_dir/loop_bad.err"
grep -qF 'Animations file loop has no value; it is ignored' "$tmp_dir/loop_bad.err"
grep -qF 'Animation "run" has an unknown setting "bounce"; it is ignored' "$tmp_dir/loop_bad.err"
grep -qF 'Animation "walk" fps "sideways" is not a number; it is ignored' "$tmp_dir/loop_bad.err"
grep -qF '"loop": "loop"' "$tmp_dir/out.loop_bad.json"

# multipack: single-texture formats write one file per atlas holding only that atlas's sprites
printf 'atlas 32,32\nsprite "a.png" rect:0,0,16,16\nsprite "b.png" rect:16,0,8,8 slice:2,2,2,2\natlas 64,16\nsprite "c.png" rect:0,0,8,8\nsprite "d.png" rect:8,0,8,8\n' > "$tmp_dir/mp.txt"
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
printf 'atlas 8,8\nsprite "a.png" rect:0,0,8,8\n' > "$tmp_dir/one.txt"
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
printf 'atlas 16,8\nsprite "a.png" rect:0,0,8,8\nsprite "b.png" rect:8,0,8,8\n' \
  | "$convert_bin" --transform phaser-anims --animations "$key_anims" -a 'hero.png' | grep -qF '"key": "hero"'

# scaled layouts: trims and markers are in original-image pixels, the packed size is scaled;
# exported metadata uses atlas pixels throughout
printf 'scale 0.5\natlas 200,250\nsprite "f.png" rect:0,0,161,228 trim:4,5,2,0\n' > "$tmp_dir/scaled.txt"
printf 'path "f.png"\n- marker "pivot" point 164,230\n- marker "hit" rectangle 10,20 30,40\n' > "$tmp_dir/scaled.markers"
"$convert_bin" --transform phaser-hash < "$tmp_dir/scaled.txt" | tr -d ' \n' > "$tmp_dir/out.scaled.phaser"
grep -qF '"sourceSize":{"h":231,"w":164}' "$tmp_dir/out.scaled.phaser"
grep -qF '"spriteSourceSize":{"h":228,"w":161,"x":2,"y":3}' "$tmp_dir/out.scaled.phaser"
"$convert_bin" --transform json --markers "$(fix_path "$tmp_dir/scaled.markers")" < "$tmp_dir/scaled.txt" | tr -d ' \n' > "$tmp_dir/out.scaled.json"
grep -qF '"pivot":{"x":82,"y":115}' "$tmp_dir/out.scaled.json"
grep -qF '"type":"rectangle","w":15,"x":5,"y":10}' "$tmp_dir/out.scaled.json"
grep -qF '"h":20,"index":1,"name":"hit"' "$tmp_dir/out.scaled.json"

# phaser applies any pivot as the origin: only write one when a pivot marker sets it
printf 'atlas 8,8\nsprite "a.png" rect:0,0,8,8\n' > "$tmp_dir/nopivot.txt"
if "$convert_bin" --transform phaser-hash < "$tmp_dir/nopivot.txt" | grep -q '"pivot"'; then
  echo "phaser frames without a pivot marker must not carry a pivot" >&2
  exit 1
fi
printf 'path "a.png"\n- marker "pivot" point 4,8\n' > "$tmp_dir/nopivot.markers"
"$convert_bin" --transform phaser-array --markers "$(fix_path "$tmp_dir/nopivot.markers")" < "$tmp_dir/nopivot.txt" \
  | tr -d ' \n' | grep -qF '"pivot":{"x":0.5,"y":1}'

# nine-patch: the 1px marker border is not part of the exported image
printf 'atlas 16,16\nsprite "padded.9.png" rect:0,0,8,8 trim:4,3,4,3 slice:4,4,2,2\n' > "$tmp_dir/ninepatch.txt"
"$convert_bin" --transform plist < "$tmp_dir/ninepatch.txt" > "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{14,12}</string>' "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{{3,2},{8,8}}</string>' "$tmp_dir/out.ninepatch.plist"
grep -qF '<string>{{4,4},{8,6}}</string>' "$tmp_dir/out.ninepatch.plist"
printf 'atlas 16,16\nsprite "plain.9.png" rect:0,0,14,12 trim:1,1,1,1 slice:4,4,2,2\n' \
  | "$convert_bin" --transform phaser-hash | tr -d ' \n' | grep -qF '"sourceSize":{"h":12,"w":14},"spriteSourceSize":{"h":12,"w":14,"x":0,"y":0},"trimmed":false'

# --image-path: the reference written into metadata; -a still names files and seeds GUIDs
printf 'atlas 32,32\nsprite "a.png" rect:0,0,16,16\natlas 32,32\nsprite "b.png" rect:0,0,8,8\n' > "$tmp_dir/ip.txt"
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
printf 'atlas 8,8\nsprite "a.png" rect:0,0,4,4\n' > "$tmp_dir/limit.txt"
printf 'path "a.png"\n- marker "pivot" point 16777216,-16777216\n' > "$tmp_dir/limit_ok.markers"
"$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_ok.markers")" < "$tmp_dir/limit.txt" > /dev/null
printf 'path "a.png"\n- marker "hit" polygon 0,0 16777217,0\n' > "$tmp_dir/limit_bad.markers"
if "$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_bad.markers")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/limit.err"; then
  echo "a marker beyond the coordinate limit should fail" >&2
  exit 1
fi
grep -qF 'Marker "hit" of sprite a.png has a value beyond 16777216 atlas pixels' "$tmp_dir/limit.err"

# Animation fps must stay within 100 (sprat-lib's SPRAT_FPS_LIMIT), file-wide and per animation.
printf 'fps 100\nanimation "run" 100\n- frame "a.png"\n' > "$tmp_dir/fps_ok.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_ok.anims")" \
    < "$tmp_dir/limit.txt" | grep -q '^anim 100 '
printf 'fps 101\nanimation "run"\n- frame "a.png"\n' > "$tmp_dir/fps_file.anims"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_file.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"; then
  echo "an animations file fps above 100 should fail" >&2
  exit 1
fi
grep -qF 'Animations file fps 101 is above the limit of 100' "$tmp_dir/fps.err"
printf 'fps 12\nanimation "run" 1000\n- frame "a.png"\n' > "$tmp_dir/fps_anim.anims"
if "$convert_bin" --transform json --animations "$(fix_path "$tmp_dir/fps_anim.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"; then
  echo "an animation fps above 100 should fail" >&2
  exit 1
fi
grep -qF 'Animation "run" has fps 1000, above the limit of 100' "$tmp_dir/fps.err"
# An fps too large for an int is still above the limit, not dropped in favor of the default.
printf 'fps 99999999999\nanimation "run"\n- frame "a.png"\n' > "$tmp_dir/fps_huge_file.anims"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_huge_file.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"; then
  echo "an animations file fps too large for an int should fail" >&2
  exit 1
fi
grep -qF 'Animations file fps 99999999999 is above the limit of 100' "$tmp_dir/fps.err"
printf 'animation "run" 4294967296\n- frame "a.png"\n' > "$tmp_dir/fps_huge_anim.anims"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_huge_anim.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"; then
  echo "an animation fps too large for an int should fail" >&2
  exit 1
fi
grep -qF 'Animation "run" has fps 4294967296, above the limit of 100' "$tmp_dir/fps.err"
# A number too negative for an int still means the default.
printf 'fps -99999999999\nanimation "run"\n- frame "a.png"\n' > "$tmp_dir/fps_neg.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_neg.anims")" \
    < "$tmp_dir/limit.txt" | grep -q '^anim 8 '
# fps reads its leading integer, as before: "12.5" is 12, "+24" is 24, "1000.0" is above 100.
printf 'fps 12.5\nanimation "run"\n- frame "a.png"\n' > "$tmp_dir/fps_dec.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_dec.anims")" \
    < "$tmp_dir/limit.txt" | grep -q '^anim 12 '
printf 'animation "run" +24\n- frame "a.png"\n' > "$tmp_dir/fps_plus.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_plus.anims")" \
    < "$tmp_dir/limit.txt" | grep -q '^anim 24 '
printf 'animation "run" 1000.0\n- frame "a.png"\n' > "$tmp_dir/fps_big_dec.anims"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_big_dec.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"; then
  echo "an animation fps of 1000.0 should fail" >&2
  exit 1
fi
grep -qF 'Animation "run" has fps 1000.0, above the limit of 100' "$tmp_dir/fps.err"
# An fps that is not a number is ignored with a warning, file-wide and per animation.
printf 'fps abc\nanimation "run" x9\n- frame "a.png"\n' > "$tmp_dir/fps_nan.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_nan.anims")" \
    < "$tmp_dir/limit.txt" 2> "$tmp_dir/fps.err" | grep -q '^anim 8 '
grep -qF 'Warning: Animations file fps "abc" is not a number; it is ignored' "$tmp_dir/fps.err"
grep -qF 'Warning: Animation "run" fps "x9" is not a number; it is ignored' "$tmp_dir/fps.err"
# A bare fps line has its own warning.
printf 'fps\nanimation "run"\n- frame "a.png"\n' > "$tmp_dir/fps_bare.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_bare.anims")" \
    < "$tmp_dir/limit.txt" 2> "$tmp_dir/fps.err" | grep -q '^anim 8 '
grep -qF 'Warning: Animations file fps has no value; it is ignored' "$tmp_dir/fps.err"
# Only the leading integer is read, with a warning when anything follows it.
printf 'fps 12abc\nanimation "run" 1O0\n- frame "a.png"\n' > "$tmp_dir/fps_partial.anims"
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_partial.anims")" \
    < "$tmp_dir/limit.txt" 2> "$tmp_dir/fps.err" | grep -q '^anim 1 '
grep -qF 'Warning: Animations file fps "12abc" is read as 12' "$tmp_dir/fps.err"
grep -qF 'Warning: Animation "run" fps "1O0" is read as 1' "$tmp_dir/fps.err"
# A valid file gives no warning.
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/fps_ok.anims")" \
    < "$tmp_dir/limit.txt" > /dev/null 2> "$tmp_dir/fps.err"
if [ -s "$tmp_dir/fps.err" ]; then
  echo "a valid animations file should not warn" >&2
  cat "$tmp_dir/fps.err" >&2
  exit 1
fi

# The limit applies once scaled: 20000000 at scale 0.5 exports as 10000000.
printf 'scale 0.5\natlas 8,8\nsprite "a.png" rect:0,0,4,4\n' > "$tmp_dir/limit_scaled.txt"
printf 'path "a.png"\n- marker "pivot" point 20000000,0\n' > "$tmp_dir/limit_scaled.markers"
"$convert_bin" --transform raw --markers "$(fix_path "$tmp_dir/limit_scaled.markers")" \
    < "$tmp_dir/limit_scaled.txt" > /dev/null

# ── hand-written markers and animations files ────────────────────────────────
cat > "$tmp_dir/hand.txt" <<'HAND'
atlas 32,8
scale 1
sprite "f/run_1.png" rect:0,0,8,8
sprite "f/run_2.png" rect:8,0,8,8
sprite "f/1.png" rect:16,0,8,8
HAND
# A path that matches no sprite drops its markers instead of making them global, where a
# mistyped pivot would move every sprite; malformed markers are reported.
cat > "$tmp_dir/hand.markers" <<'HANDM'
path "f/typo.png"
- marker "pivot" point 3,3
path "f/run_1.png"
- marker "pivot" point 5 5
- marker "x" pnt 3,3
- marker "poly" polygon 1,1 2,x 3,3
- marker "ok" point 1,2
HANDM
"$convert_bin" --transform json --markers "$(fix_path "$tmp_dir/hand.markers")" \
    < "$tmp_dir/hand.txt" > "$tmp_dir/hand.json" 2> "$tmp_dir/hand.err"
python3 - "$tmp_dir/hand.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for s in d["sprites"]:
    assert s["pivot"] == {"x": 0, "y": 0}, s
run_1 = d["sprites"][0]
assert [m["name"] for m in run_1["markers"]] == ["ok"], run_1["markers"]
PY
grep -qF 'Warning: Markers file path "f/typo.png" matches no sprite; its markers are ignored' "$tmp_dir/hand.err"
grep -qF 'Warning: Marker "pivot" has invalid point values; it is ignored' "$tmp_dir/hand.err"
grep -qF 'Warning: Marker "x" has an unknown type "pnt"' "$tmp_dir/hand.err"
grep -qF 'Warning: Marker "poly" has invalid polygon values; it is ignored' "$tmp_dir/hand.err"

# The fps and loop lines are defaults wherever they appear; unresolved frames, frames
# outside an animation and bad flips are reported; a bare number is a sprite index.
cat > "$tmp_dir/hand.anims" <<'HANDA'
- frame 0
animation "a"
- frame "nope.png"
- frame 99
- frame 1
fps 30
loop once
animation "b" 12 loop
- frame "f/1.png"
animation "c" alias "b" flip both
animation "d" alias "b" flip vh
HANDA
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/hand.anims")" \
    < "$tmp_dir/hand.txt" > "$tmp_dir/hand.sprat" 2> "$tmp_dir/hand.err"
grep -qxF 'anim 30 0 1 -1 1 1 1:a' "$tmp_dir/hand.sprat"
grep -qxF 'anim 12 0 0 -1 1 2 1:b' "$tmp_dir/hand.sprat"
grep -qxF 'anim 30 0 0 1 0 1:c' "$tmp_dir/hand.sprat"
grep -qxF 'anim 30 3 0 1 0 1:d' "$tmp_dir/hand.sprat"
grep -qF 'Warning: Frame line "- frame 0" is not inside an animation' "$tmp_dir/hand.err"
grep -qF 'Warning: Animation "a" frame "nope.png" matches no sprite; it is ignored' "$tmp_dir/hand.err"
grep -qF 'Warning: Animation "a" frame 99 is out of range (the layout has 3 sprites); it is ignored' "$tmp_dir/hand.err"
grep -qF 'Warning: Animation "c" flip "both" is not one of h, v, hv; it is ignored' "$tmp_dir/hand.err"

# Animation names must be unique; one from the file replaces the automatic one.
printf 'animation "a"\n- frame 0\nanimation "a"\n- frame 1\n' > "$tmp_dir/dup.anims"
if "$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/dup.anims")" \
    < "$tmp_dir/hand.txt" > /dev/null 2> "$tmp_dir/dup.err"; then
  echo "duplicate animation names should be rejected" >&2
  exit 1
fi
grep -qF 'Animation "a" is defined more than once' "$tmp_dir/dup.err"
printf 'animation "run"\n- frame 1\n' > "$tmp_dir/auto.anims"
"$convert_bin" --transform raw --auto-animations --animations "$(fix_path "$tmp_dir/auto.anims")" \
    < "$tmp_dir/hand.txt" > "$tmp_dir/auto.sprat"
test "$(grep -c '^anim ' "$tmp_dir/auto.sprat")" -eq 1
grep -qxF 'anim 8 0 0 -1 1 1 3:run' "$tmp_dir/auto.sprat"

# The layout stream can carry markers and animations, loop lines and comments included;
# a line that only starts like one of their keywords is still an error.
{ cat "$tmp_dir/hand.txt"; printf '# comment\nloop once\nanimation "a"\n- frame 0\n'; } \
    | "$convert_bin" --transform raw | grep -qxF 'anim 8 0 1 -1 1 0 1:a'
if { cat "$tmp_dir/hand.txt"; printf 'pathological\n'; } | "$convert_bin" --transform json > /dev/null 2>&1; then
  echo "a line that only starts with a keyword should be rejected" >&2
  exit 1
fi
if printf 'atlas 8,8\nroot "x" junk\n' | "$convert_bin" --transform json > /dev/null 2>&1; then
  echo "text after the root path should be rejected" >&2
  exit 1
fi

# Text outputs write the scale as given, not with Jsonnet's 17 digits.
sed 's/^scale 1$/scale 0.1/' "$tmp_dir/hand.txt" > "$tmp_dir/scale.txt"
"$convert_bin" --transform raw < "$tmp_dir/scale.txt" | grep -q '^sheet 0.1 '
"$convert_bin" --transform xml < "$tmp_dir/scale.txt" | grep -qF 'scale="0.1"'

# ── Nothing in a markers or animations file is skipped silently ─────────────
cat > "$tmp_dir/strict.markers" <<'STRICTM'
pth "f/run_1.png"
- markr "x" point 1,1
root ./frames
path "f/run_1.png" junk
path "f/run_2.png"
- marker "p" point 1,1 junk
- marker "pivot" point 1,1
- marker "pivot" point 2,2
animation "x"
STRICTM
"$convert_bin" --transform json --markers "$(fix_path "$tmp_dir/strict.markers")" \
    < "$tmp_dir/hand.txt" > /dev/null 2> "$tmp_dir/strict.err"
for expected in \
    'Markers file line "pth "f/run_1.png"" is not a root, path or - marker line' \
    'Markers file line "- markr "x" point 1,1" is not a root, path or - marker line' \
    'Markers file line "root ./frames" is not root "DIRECTORY"' \
    'Markers file line "path "f/run_1.png" junk" is not path "SPRITE"' \
    'Marker "p" has invalid point values' \
    'Sprite "f/run_2.png" has more than one pivot; only the first is used' \
    'Markers file line "animation "x"" is not a root, path or - marker line'; do
  grep -qF "$expected" "$tmp_dir/strict.err" || { echo "missing warning: $expected" >&2; cat "$tmp_dir/strict.err" >&2; exit 1; }
done
cat > "$tmp_dir/strict.anims" <<'STRICTA'
animaton "x"
- fram 0
fps 12 13
loop once twice
path "f/run_1.png"
animation "empty"
STRICTA
"$convert_bin" --transform json --animations "$(fix_path "$tmp_dir/strict.anims")" \
    < "$tmp_dir/hand.txt" > /dev/null 2> "$tmp_dir/strict.err"
for expected in \
    'Animations file line "animaton "x"" is not a root, fps, loop, animation or - frame line' \
    'Animations file line "- fram 0" is not a root, fps, loop, animation or - frame line' \
    'Animations file line "fps 12 13" has more than one fps' \
    'Animations file line "loop once twice" has more than one loop mode' \
    'Animations file line "path "f/run_1.png"" is not a root, fps, loop, animation or - frame line' \
    'Animation "empty" has no frames'; do
  grep -qF "$expected" "$tmp_dir/strict.err" || { echo "missing warning: $expected" >&2; cat "$tmp_dir/strict.err" >&2; exit 1; }
done
# In the layout stream, each kind of line belongs to someone: no warnings.
{ cat "$tmp_dir/hand.txt"
  printf 'path "f/run_1.png"\n- marker "pivot" point 1,1\nfps 12\nanimation "run"\n- frame 0 1\n'; } \
    | "$convert_bin" --transform json > /dev/null 2> "$tmp_dir/strict.err"
if [ -s "$tmp_dir/strict.err" ]; then
  echo "a layout stream with markers and animations should not warn" >&2
  cat "$tmp_dir/strict.err" >&2
  exit 1
fi

# ── Several frames per line, and ranges of indexes or names ─────────────────
cat > "$tmp_dir/frames.txt" <<'FRAMES'
atlas 64,8
sprite "f/run_01.png" rect:0,0,8,8
sprite "f/run_02.png" rect:8,0,8,8
sprite "f/run_03.png" rect:16,0,8,8
sprite "f/idle 1.png" rect:24,0,8,8
sprite "f/idle 2.png" rect:32,0,8,8
FRAMES
cat > "$tmp_dir/frames.anims" <<'FRAMESA'
animation "run"
- frame run_01..run_03 run_02
animation "back"
- frame 2..0
- frame "f/idle 2.png"
animation "idle"
- frame "f/idle 1.png".."f/idle 2.png"
animation "mixed"
- frame run_01..walk_03
- frame 0..9999999
- frame "f/idle 1.png"..idle
FRAMESA
"$convert_bin" --transform raw --animations "$(fix_path "$tmp_dir/frames.anims")" \
    < "$tmp_dir/frames.txt" > "$tmp_dir/frames.sprat" 2> "$tmp_dir/frames.err"
grep -qxF 'anim 8 0 0 -1 4 0 1 2 1 3:run' "$tmp_dir/frames.sprat"
grep -qxF 'anim 8 0 0 -1 4 2 1 0 4 4:back' "$tmp_dir/frames.sprat"
grep -qxF 'anim 8 0 0 -1 2 3 4 4:idle' "$tmp_dir/frames.sprat"
grep -qF 'Animation "mixed" frame "run_01..walk_03" matches no sprite' "$tmp_dir/frames.err"
grep -qF 'Animation "mixed" range "0..9999999" is too long' "$tmp_dir/frames.err"
grep -qF 'Frame line "- frame "f/idle 1.png"..idle" has a broken quote or range' "$tmp_dir/frames.err"
grep -qF 'Animation "mixed" has no frames' "$tmp_dir/frames.err"

# ── Nested fields in a transform result must not shadow top-level ones ───────
cat > "$tmp_dir/nested.jsonnet" <<'NESTED'
{
  name: "nested",
  extension: ".txt",
  a: { content: "WRONG", extension: ".wrong", files: [] },
  content: "RIGHT",
}
NESTED
"$convert_bin" --transform "$(fix_path "$tmp_dir/nested.jsonnet")" < "$layout_file" \
    > "$tmp_dir/nested.out"
if [ "$(cat "$tmp_dir/nested.out")" != "RIGHT" ]; then
    echo "nested transform fields shadowed the top-level content" >&2
    cat "$tmp_dir/nested.out" >&2
    exit 1
fi
mkdir -p "$tmp_dir/nested_out"
"$convert_bin" --transform "$(fix_path "$tmp_dir/nested.jsonnet")" \
    --output-dir "$(fix_path "$tmp_dir/nested_out")" < "$layout_file"
[ -f "$tmp_dir/nested_out/nested.txt" ] || {
    echo "nested transform extension shadowed the top-level extension" >&2
    ls -la "$tmp_dir/nested_out" >&2
    exit 1
}

echo "convert_test.sh: ok"
