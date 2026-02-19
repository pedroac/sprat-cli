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
grep -q '^index,name,path,x,y,w,h,trim_left,trim_top,trim_right,trim_bottom,marker_count,markers_json$' "$tmp_dir/out.csv"
grep -q '^1,b,./frames/b.png,16,0,8,8,1,2,3,4,0,\[\]$' "$tmp_dir/out.csv"

"$convert_bin" --transform xml < "$layout_file" > "$tmp_dir/out.xml"
grep -q '^<atlas width="64" height="32" scale="1">$' "$tmp_dir/out.xml"
grep -q 'trim_left="1" trim_top="2" trim_right="3" trim_bottom="4"' "$tmp_dir/out.xml"

"$convert_bin" --transform css < "$layout_file" > "$tmp_dir/out.css"
grep -Fq '.sprite-1 {' "$tmp_dir/out.css"
grep -q '^  background-position: -16px -0px;$' "$tmp_dir/out.css"

custom_transform="$tmp_dir/custom.transform"
cat > "$custom_transform" <<'CUSTOM'
[meta]
name=custom
[/meta]

[header]
BEGIN {{atlas_width}}x{{atlas_height}} count={{sprite_count}}
[/header]

[sprites]
  [sprite]
{{index}}|{{path}}|{{x}},{{y}} {{w}}x{{h}}
  [/sprite]
[/sprites]

[separator]
;
[/separator]

[footer]

END
[/footer]
CUSTOM

"$convert_bin" --transform "$custom_transform" < "$layout_file" > "$tmp_dir/out.custom"
grep -q '^BEGIN 64x32 count=2' "$tmp_dir/out.custom"
grep -q '1|./frames/b.png|16,0 8x8' "$tmp_dir/out.custom"

markers_file="$tmp_dir/markers.json"
cat > "$markers_file" <<'MARKERS'
{
  "sprites": {
    "./frames/a.png": {"markers": [{"name": "hit", "type": "point", "x": 3, "y": 5}, {"name": "hurt", "type": "circle", "x": 6, "y": 7, "radius": 4}]},
    "b": {"markers": [{"name": "foot", "type": "rectangle", "x": 1, "y": 2, "w": 3, "h": 4}]}
  }
}
MARKERS

animations_file="$tmp_dir/animations.json"
cat > "$animations_file" <<'ANIMS'
{
  "timelines": [
    {"name": "run", "frames": ["./frames/a.png", "b"]},
    {"name": "idle", "sprite_indexes": [1]}
  ]
}
ANIMS

extras_transform="$tmp_dir/extras.transform"
cat > "$extras_transform" <<'EXTRAS'
[meta]
name=extras
[/meta]

[header]
markers={{has_markers}} animations={{has_animations}}
markers_path={{markers_path}}
animations_path={{animations_path}}
marker_count={{marker_count}}
animation_count={{animation_count}}

[/header]

[sprites]
  [sprite]
{{index}}|{{name}}|{{path}}|{{sprite_markers_count}}|{{sprite_markers_json}}
  [/sprite]
[/sprites]
EXTRAS

"$convert_bin" --transform "$extras_transform" --markers "$markers_file" --animations "$animations_file" < "$layout_file" > "$tmp_dir/out.extras"
grep -q '^markers=true animations=true$' "$tmp_dir/out.extras"
grep -q "^markers_path=$markers_file$" "$tmp_dir/out.extras"
grep -q "^animations_path=$animations_file$" "$tmp_dir/out.extras"
grep -q '^marker_count=3$' "$tmp_dir/out.extras"
grep -q '^animation_count=2$' "$tmp_dir/out.extras"
grep -Fq '0|a|./frames/a.png|2|[{"name":"hit","type":"point","x":3,"y":5},{"name":"hurt","type":"circle","x":6,"y":7,"radius":4}]' "$tmp_dir/out.extras"
grep -Fq '1|b|./frames/b.png|1|[{"name":"foot","type":"rectangle","x":1,"y":2,"w":3,"h":4}]' "$tmp_dir/out.extras"

iter_transform="$tmp_dir/iter.transform"
cat > "$iter_transform" <<'ITER'
[meta]
name=iter
[/meta]

[header]
BEGIN

[/header]

[if_markers]
M_ON

[/if_markers]

[markers_header]
M_BEGIN

[/markers_header]

[markers]
  [marker]
M{{marker_index}}={{marker_name}}@{{marker_sprite_index}}:{{marker_sprite_name}}

  [/marker]
[/markers]

[markers_separator]
|
[/markers_separator]

[markers_footer]
M_END

[/markers_footer]

[if_no_markers]
M_EMPTY

[/if_no_markers]

[sprites]
  [sprite]
S{{index}}={{path}}

  [/sprite]
[/sprites]

[separator]
;

[/separator]

[if_animations]
A_ON

[/if_animations]

[animations_header]
A_BEGIN

[/animations_header]

[animations]
  [animation]
A{{animation_index}}={{animation_name}}:[{{animation_sprite_indexes}}]

  [/animation]
[/animations]

[animations_separator]
|
[/animations_separator]

[animations_footer]
A_END

[/animations_footer]

[if_no_animations]
A_EMPTY

[/if_no_animations]

[footer]
END
[/footer]
ITER

"$convert_bin" --transform "$iter_transform" --markers "$markers_file" --animations "$animations_file" < "$layout_file" > "$tmp_dir/out.iter.full"
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

"$convert_bin" --transform "$iter_transform" < "$layout_file" > "$tmp_dir/out.iter.empty"
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

"$convert_bin" --transform json --markers "$markers_file" --animations "$animations_file" < "$layout_file" > "$tmp_dir/out.builtin.json"
python3 -m json.tool "$tmp_dir/out.builtin.json" > /dev/null
grep -q '"animations": \[' "$tmp_dir/out.builtin.json"
grep -q '"sprites": \[' "$tmp_dir/out.builtin.json"
grep -q '"name": "a"' "$tmp_dir/out.builtin.json"
grep -q '"markers": \[{"name":"hit","type":"point","x":3,"y":5},{"name":"hurt","type":"circle","x":6,"y":7,"radius":4}\]' "$tmp_dir/out.builtin.json"
grep -q '"name": "run"' "$tmp_dir/out.builtin.json"
grep -q '"fps": 8' "$tmp_dir/out.builtin.json"
grep -q '"sprite_indexes": \[0,1\]' "$tmp_dir/out.builtin.json"
if grep -q '"index":' "$tmp_dir/out.builtin.json"; then
  echo "builtin json transform should not include index fields in sprite/animation objects" >&2
  exit 1
fi
sprites_line="$(grep -n '"sprites": \[' "$tmp_dir/out.builtin.json" | head -n1 | cut -d: -f1)"
animations_line="$(grep -n '"animations": \[' "$tmp_dir/out.builtin.json" | head -n1 | cut -d: -f1)"
if [ "$animations_line" -le "$sprites_line" ]; then
  echo "animations section must be outside and after sprites in json transform" >&2
  exit 1
fi

echo "convert_test.sh: ok"
