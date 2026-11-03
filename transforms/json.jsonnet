// json.jsonnet – Generic JSON metadata format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local sprite_obj(s) = {
  name: s.name,
  path: s.path,
  atlas_index: s.atlas_index,
  rect: { x: s.x, y: s.y, w: s.w, h: s.h },
  pivot: { x: s.pivot_x, y: s.pivot_y },
  trim: { left: s.trim_left, top: s.trim_top, right: s.trim_right, bottom: s.trim_bottom },
  markers: s.markers,
  rotation: lib.rotation_degrees(sprat, s),
  has_slice: s.has_slice,
} + (if s.alias_of != "" then { alias_of: s.alias_of } else {})
  + (if s.has_slice then {
  slice: {
    left: s.slice_left,
    top: s.slice_top,
    right: s.slice_right,
    bottom: s.slice_bottom,
    h_mode: s.slice_h,
    v_mode: s.slice_v,
  },
} else {});

local anim_obj(a) =
  if a.is_alias then
    { name: a.name, alias: a.alias_source }
    + (if a.flip != "" then { flip: a.flip } else {})
  else
    { name: a.name, fps: a.fps, loop: a.loop, sprite_indexes: a.frame_indices, sprite_names: [f.name for f in a.frames] }
    + (if a.flip != "" then { flip: a.flip } else {});

local atlas_obj(at) = {
  width: at.width,
  height: at.height,
  path: at.path,
};

local result = {
  multipack: sprat.multipack,
  scale: sprat.scale,
  extrude: sprat.extrude,
  atlases: [atlas_obj(at) for at in sprat.atlases],
  sprites: [sprite_obj(s) for s in sprat.sprites],
} +
(if sprat.has_animations then { animations: [anim_obj(a) for a in sprat.animations] } else {}) +
// Markers given before any path in the markers file, which belong to no one sprite.
(local globals = [m for m in sprat.markers if m.sprite_index < 0];
 if std.length(globals) > 0 then { markers: globals } else {});

{
  name: "JSON",
  description: "JSON metadata for scripting and runtime loading",
  extension: ".json",
  icon: "icons/json-svgrepo-com.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
