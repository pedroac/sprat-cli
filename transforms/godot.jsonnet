// godot.jsonnet – Godot-compatible JSON sprite sheet.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_obj(s) = {
  name: s.name,
  region: { x: s.x, y: s.y, w: s.w, h: s.h },
  margin: { left: s.trim_left, top: s.trim_top, right: s.trim_right, bottom: s.trim_bottom },
  source_size: { w: s.source_w, h: s.source_h },
  rotated: s.rotated,
  pivot_offset: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw },
} + (if s.has_slice then {
  slice: {
    left: s.slice_left,
    top: s.slice_top,
    right: s.slice_right,
    bottom: s.slice_bottom,
    h_mode: s.slice_h,
    v_mode: s.slice_v,
  },
} else {});

// Godot animations use from/to indices (first and last frame index).
local anim_obj(a) = {
  name: a.name,
  from: if std.length(a.frame_indices) > 0 then a.frame_indices[0] else 0,
  to: if std.length(a.frame_indices) > 0 then a.frame_indices[std.length(a.frame_indices) - 1] else 0,
  speed: a.fps,
  loop: true,
};

local result =
  {
    image: sprat.atlas_path,
    size: { w: sprat.atlas_width, h: sprat.atlas_height },
    scale: sprat.scale,
    frames: [frame_obj(s) for s in sprat.sprites],
  } +
  (if sprat.has_normal_atlas then { normal_image: sprat.normal_atlas_path } else {}) +
  (if sprat.has_collision_atlas then {
    collision_image: sprat.collision_atlas_path,
    collision_scale: sprat.collision_atlas_scale,
  } else {}) +
  (if sprat.has_animations then {
    animations: [anim_obj(a) for a in sprat.animations],
  } else {});

{
  name: "Godot",
  description: "Godot-compatible JSON sprite sheet (load at runtime with AtlasTexture/SpriteFrames)",
  extension: ".json",
  icon: "icons/godot-svgrepo-com(1).svg",
  content: std.manifestJsonEx(result, " ") + "\n",
}
