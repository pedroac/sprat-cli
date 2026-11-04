// raw.jsonnet – Plain-token metadata that a C loader reads in one pass, with no parser
// library (see sprat-lib). Every value is a space-separated token, one record per line:
//
//   sprat-raw 7
//   sheet SCALE EXTRUDE MULTIPACK ATLASES SPRITES ANIMATIONS PUPPETS GLOBAL_MARKERS MARKERS
//         VERTICES FRAMES PARTS VARIANTS JOINTS POSES SETTINGS STRING_BYTES
//   atlas W H PATH                                                (ATLASES lines)
//   marker TYPE X Y RADIUS W H VERTICES [VX VY]... NAME           (GLOBAL_MARKERS lines)
//   sprite ATLAS X Y W H ROTATION TRIM_L TRIM_T TRIM_R TRIM_B HAS_PIVOT PIVOT_X PIVOT_Y
//          HAS_SLICE SLICE_L SLICE_T SLICE_R SLICE_B SLICE_H SLICE_V ALIAS MARKERS NAME PATH
//   marker TYPE X Y RADIUS W H VERTICES [VX VY]... NAME           (MARKERS lines after a sprite)
//   anim FPS FLIP LOOP ALIAS FRAMES [SPRITE_INDEX]... NAME
//   puppet PARTS JOINTS POSES NAME                                (PUPPETS lines)
//   part PARENT HAS_POSITION X Y VARIANTS NAME                    (PARTS lines after a puppet)
//   variant KIND INDEX NAME                                       (VARIANTS lines after a part)
//   joint CHILD PARENT CONSTRAINT LEEWAY HAS_ANGLE ANGLE_MIN ANGLE_MAX CHILD_MARKER
//         PARENT_MARKER                                           (JOINTS lines after the parts)
//   pose SETTINGS NAME                                            (POSES lines after the joints)
//   set PART VARIANT HAS_ANGLE ANGLE HAS_OFFSET OFFSET_X OFFSET_Y HAS_POSITION X Y
//                                                                 (SETTINGS lines after a pose)
//
// Strings are length-prefixed ("<utf8 bytes>:<bytes>"), so they may hold spaces or newlines.
// An empty string means "none". Enums are numbers: marker type point=0 circle=1 rectangle=2
// polygon=3; slice mode stretch=0 repeat=1 mirror=2 fixed=3; FLIP bits h=1 v=2; LOOP bits
// once=1 pingpong=2 (loop=0, once=1, pingpong=2, once-pingpong=3). On an anim line, ALIAS is
// the index of the animation an alias replays (its FRAMES is 0), or -1; an alias's LOOP is its
// source's. On a sprite line, ALIAS is the index of the regular sprite whose atlas region a
// deduplicated sprite shares, or -1. HAS_PIVOT tells a pivot at 0,0 from no pivot (then
// PIVOT_X and PIVOT_Y are 0). Global markers are the markers file's markers before any path.
// A puppet is a cutout rig. Its parts come in draw order, back first; PARENT is the index,
// within the puppet, of the part it is attached to, or -1. A part that isn't attached has
// HAS_POSITION 1 and is placed by X Y: where its sprite's pivot goes in the puppet's space,
// in atlas pixels with Y down, whose origin is the puppet's pivot; an attached part has
// HAS_POSITION 0 and X Y 0. A variant is what a part can show:
// KIND sprite=0 (INDEX is a sprite index) or animation=1 (INDEX is an animation index). A
// joint pins the child part's CHILD_MARKER point to the parent part's PARENT_MARKER, both
// looked up by name in the markers of the sprite each part shows; the parent marker's shape
// (a point, or the inside of a circle, rectangle or polygon) is where the child's point may
// be, and LEEWAY (atlas pixels) how far it may drift beyond it. With HAS_ANGLE, the child
// turns ANGLE_MIN to ANGLE_MAX degrees (clockwise, y down) from its rest angle relative to
// its parent; without it, it turns freely. A part's parent joint (CONSTRAINT 0) is the one
// whose CHILD is that part and whose PARENT is the part's PARENT; every part with a parent
// has exactly one. Joints with CONSTRAINT 1 are further attachments, which may close loops:
// an editor, or an engine that wants live physics, keeps them within their shape and
// leeway while posing, and playing a pose needs none of them.
// A pose sets some parts of its puppet, in part order, each part at most once; parts it
// doesn't set keep what they had, so poses layer. VARIANT is an index into the part's
// variants, or -1 to keep it. With HAS_ANGLE, ANGLE (degrees, clockwise) turns the part
// from its rest angle: relative to its parent, within its parent joint's limits, or around
// its own pivot for a positioned part. With HAS_OFFSET, OFFSET_X OFFSET_Y (atlas pixels)
// moves an attached part's point from the parent marker's x,y, within the marker's shape
// and the leeway. With HAS_POSITION, X Y moves a positioned part. Each value is 0 without
// its flag.
// Version 7 removed the puppet's REFERENCE and added POSES and SETTINGS, each joint's
// CONSTRAINT, and the pose records.
// Version 6 added the puppet's REFERENCE and each part's HAS_POSITION X Y.
// Version 5 added PUPPETS, PARTS, VARIANTS and JOINTS and the puppet records.
// Version 4 added GLOBAL_MARKERS and their lines, and HAS_PIVOT, and replaced the sprite's
// ALIAS_OF path (last on the line) with the ALIAS index. Version 3 dropped the normal and collision map fields (COLLISION_SCALE and NORMAL_PATH,
// COLLISION_PATH on the sheet and atlas lines). Version 2 added LOOP; version 1 files have
// none, and play as loop.
//
// The sheet line's MARKERS, VERTICES, FRAMES, PARTS, VARIANTS, JOINTS, POSES, SETTINGS and
// STRING_BYTES are the totals over the whole file: marker lines (global ones included),
// polygon vertices, animation frame indexes, puppet parts, variants, joints, poses and pose
// settings, and the UTF-8 bytes of every string. With them a loader knows the exact size of the model before reading the records, so
// it can allocate it once; a loader must reject a file whose records don't match them.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local str_bytes(s) = std.length(std.encodeUTF8(s));
local str(s) = str_bytes(s) + ":" + s;
local sum(xs) = std.foldl(function(acc, x) acc + x, xs, 0);
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

// Deduplicated sprites name their canonical sprite by path; the raw format uses its index.
local sprite_index = { [sprat.sprites[i].path]: i for i in std.range(0, std.length(sprat.sprites) - 1) };
local alias_index(s) =
  if s.alias_of == "" then -1
  else if std.objectHas(sprite_index, s.alias_of) then sprite_index[s.alias_of]
  else error "sprite '" + s.path + "' is an alias of unknown sprite '" + s.alias_of + "'";

local sprite_lines(s) =
  local slice = if s.has_slice then
    [1, s.slice_left, s.slice_top, s.slice_right, s.slice_bottom,
     slice_modes[s.slice_h], slice_modes[s.slice_v]]
  else [0, 0, 0, 0, 0, 0, 0];
  line(["sprite", s.atlas_index, s.x, s.y, s.w, s.h, lib.rotation_degrees(sprat, s),
        s.trim_left, s.trim_top, s.trim_right, s.trim_bottom,
        bool(s.has_pivot), s.pivot_x, s.pivot_y]
       + slice
       + [alias_index(s), std.length(s.markers), str(s.name), str(s.path)])
  + std.join("", [marker_line(m) for m in s.markers]);

local anim_index = { [sprat.animations[i].name]: i for i in std.range(0, std.length(sprat.animations) - 1) };
local flip_bits(f) =
  (if std.length(std.findSubstr("h", f)) > 0 then 1 else 0)
  + (if std.length(std.findSubstr("v", f)) > 0 then 2 else 0);

local loop_bits = { loop: 0, once: 1, pingpong: 2, "once-pingpong": 3 };

local anim_line(a) =
  local frames = if a.is_alias then [] else a.frame_indices;
  local alias = if !a.is_alias then -1
                else if std.objectHas(anim_index, a.alias_source) then anim_index[a.alias_source]
                else error "animation '" + a.name + "' is an alias of unknown animation '" + a.alias_source + "'";
  line(["anim", a.fps, flip_bits(a.flip), loop_bits[a.loop], alias, std.length(frames)] + frames + [str(a.name)]);

local animations = if sprat.has_animations then sprat.animations else [];

local variant_kinds = { sprite: 0, animation: 1 };
local variant_line(v) =
  line(["variant", variant_kinds[v.kind],
        if v.kind == "animation" then v.animation_index else v.sprite_index, str(v.name)]);
local part_lines(p) =
  line(["part", p.parent, bool(p.has_position), p.x, p.y, std.length(p.variants), str(p.name)])
  + std.join("", [variant_line(v) for v in p.variants]);
local joint_line(jt) =
  line(["joint", jt.child, jt.parent, bool(jt.is_constraint), jt.leeway, bool(jt.has_angle),
        jt.angle_min, jt.angle_max, str(jt.child_marker), str(jt.parent_marker)]);
local set_line(st) =
  line(["set", st.part, st.variant, bool(st.has_angle), st.angle, bool(st.has_offset),
        st.offset_x, st.offset_y, bool(st.has_position), st.x, st.y]);
local pose_lines(po) =
  line(["pose", std.length(po.parts), str(po.name)])
  + std.join("", [set_line(st) for st in po.parts]);
local puppet_lines(pp) =
  line(["puppet", std.length(pp.parts), std.length(pp.joints), std.length(pp.poses), str(pp.name)])
  + std.join("", [part_lines(p) for p in pp.parts])
  + std.join("", [joint_line(jt) for jt in pp.joints])
  + std.join("", [pose_lines(po) for po in pp.poses]);

local puppets = sprat.puppets;
local parts = [p for pp in puppets for p in pp.parts];
local joints = [jt for pp in puppets for jt in pp.joints];
local poses = [po for pp in puppets for po in pp.poses];

local global_markers = [m for m in sprat.markers if m.sprite_index < 0];

// Totals for the sheet line, computed from exactly what the records below write.
local all_markers = global_markers + [m for s in sprat.sprites for m in s.markers];
local vertex_count(m) = if std.objectHas(m, "vertices") then std.length(m.vertices) else 0;
local frame_count(a) = if a.is_alias then 0 else std.length(a.frame_indices);
local string_bytes =
  sum([str_bytes(at.path) for at in sprat.atlases])
  + sum([str_bytes(s.name) + str_bytes(s.path) for s in sprat.sprites])
  + sum([str_bytes(m.name) for m in all_markers])
  + sum([str_bytes(a.name) for a in animations])
  + sum([str_bytes(pp.name) for pp in puppets])
  + sum([str_bytes(p.name) + sum([str_bytes(v.name) for v in p.variants]) for p in parts])
  + sum([str_bytes(jt.child_marker) + str_bytes(jt.parent_marker) for jt in joints])
  + sum([str_bytes(po.name) for po in poses]);

local header =
  "sprat-raw 7\n"
  + line(["sheet", sprat.scale_text, sprat.extrude, bool(sprat.multipack),
          std.length(sprat.atlases), std.length(sprat.sprites), std.length(animations),
          std.length(puppets), std.length(global_markers), std.length(all_markers),
          sum([vertex_count(m) for m in all_markers]), sum([frame_count(a) for a in animations]),
          std.length(parts), sum([std.length(p.variants) for p in parts]), std.length(joints),
          std.length(poses), sum([std.length(po.parts) for po in poses]), string_bytes]);

{
  name: "Raw",
  description: "Plain-token metadata for fast loading from C (sprat-lib)",
  extension: ".sprat",
  content: header
    + std.join("", [line(["atlas", at.width, at.height, str(at.path)])
                    for at in sprat.atlases])
    + std.join("", [marker_line(m) for m in global_markers])
    + std.join("", [sprite_lines(s) for s in sprat.sprites])
    + std.join("", [anim_line(a) for a in animations])
    + std.join("", [puppet_lines(pp) for pp in puppets]),
}
