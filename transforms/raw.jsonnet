// raw.jsonnet – Plain-token metadata that a C loader reads in one pass, with no parser
// library (see sprat-lib). Every value is a space-separated token, one record per line:
//
//   sprat-raw 1
//   sheet SCALE EXTRUDE MULTIPACK COLLISION_SCALE ATLASES SPRITES ANIMATIONS NORMAL_PATH COLLISION_PATH
//   atlas W H PATH NORMAL_PATH COLLISION_PATH
//   sprite ATLAS X Y W H ROTATION TRIM_L TRIM_T TRIM_R TRIM_B PIVOT_X PIVOT_Y
//          HAS_SLICE SLICE_L SLICE_T SLICE_R SLICE_B SLICE_H SLICE_V MARKERS NAME PATH ALIAS_OF
//   marker TYPE X Y RADIUS W H VERTICES [VX VY]... NAME          (MARKERS lines after a sprite)
//   anim FPS FLIP ALIAS FRAMES [SPRITE_INDEX]... NAME
//
// Strings are length-prefixed ("<utf8 bytes>:<bytes>"), so they may hold spaces or newlines.
// An empty string means "none". Enums are numbers: marker type point=0 circle=1 rectangle=2
// polygon=3; slice mode stretch=0 repeat=1 mirror=2 fixed=3; FLIP bits h=1 v=2. ALIAS is the
// index of the animation an alias replays (its FRAMES is 0), or -1.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local str(s) = std.length(std.encodeUTF8(s)) + ":" + s;
local bool(b) = if b then "1" else "0";
local line(tokens) = std.join(" ", ["" + t for t in tokens]) + "\n";

local marker_types = { point: 0, circle: 1, rectangle: 2, polygon: 3 };
local slice_modes = { stretch: 0, repeat: 1, mirror: 2, fixed: 3 };
local field(o, k) = if std.objectHas(o, k) then o[k] else 0;

local marker_line(m) =
  local vertices = if std.objectHas(m, "vertices") then m.vertices else [];
  line(["marker", marker_types[m.type], field(m, "x"), field(m, "y"),
        field(m, "radius"), field(m, "w"), field(m, "h"), std.length(vertices)]
       + std.flattenArrays([[v.x, v.y] for v in vertices])
       + [str(m.name)]);

local sprite_lines(s) =
  local slice = if s.has_slice then
    [1, s.slice_left, s.slice_top, s.slice_right, s.slice_bottom,
     slice_modes[s.slice_h], slice_modes[s.slice_v]]
  else [0, 0, 0, 0, 0, 0, 0];
  line(["sprite", s.atlas_index, s.x, s.y, s.w, s.h, lib.rotation_degrees(sprat, s),
        s.trim_left, s.trim_top, s.trim_right, s.trim_bottom, s.pivot_x, s.pivot_y]
       + slice
       + [std.length(s.markers), str(s.name), str(s.path), str(s.alias_of)])
  + std.join("", [marker_line(m) for m in s.markers]);

local anim_index = { [sprat.animations[i].name]: i for i in std.range(0, std.length(sprat.animations) - 1) };
local flip_bits(f) =
  (if std.length(std.findSubstr("h", f)) > 0 then 1 else 0)
  + (if std.length(std.findSubstr("v", f)) > 0 then 2 else 0);

local anim_line(a) =
  local frames = if a.is_alias then [] else a.frame_indices;
  local alias = if !a.is_alias then -1
                else if std.objectHas(anim_index, a.alias_source) then anim_index[a.alias_source]
                else error "animation '" + a.name + "' is an alias of unknown animation '" + a.alias_source + "'";
  line(["anim", a.fps, flip_bits(a.flip), alias, std.length(frames)] + frames + [str(a.name)]);

local animations = if sprat.has_animations then sprat.animations else [];

local header =
  "sprat-raw 1\n"
  + line(["sheet", sprat.scale, sprat.extrude, bool(sprat.multipack),
          if sprat.has_collision_atlas then sprat.collision_atlas_scale else 0,
          std.length(sprat.atlases), std.length(sprat.sprites), std.length(animations),
          str(if sprat.has_normal_atlas then sprat.normal_atlas_path else ""),
          str(if sprat.has_collision_atlas then sprat.collision_atlas_path else "")]);

{
  name: "Raw",
  description: "Plain-token metadata for fast loading from C (sprat-lib)",
  extension: ".sprat",
  content: header
    + std.join("", [line(["atlas", at.width, at.height, str(at.path), str(at.normal_path), str(at.collision_path)])
                    for at in sprat.atlases])
    + std.join("", [sprite_lines(s) for s in sprat.sprites])
    + std.join("", [anim_line(a) for a in animations]),
}
