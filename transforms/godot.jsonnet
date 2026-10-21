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

// frames lists every frame in play order (indices into frames[]); from/to are only
// emitted when the frames form one ascending run, so they never misdescribe the animation.
local anim_obj(a) =
  local runs = lib.consecutive_runs(a.frame_indices);
  {
    name: a.name,
    frames: a.frame_indices,
    speed: a.fps,
    loop: true,
  } + (if std.length(runs) == 1 then { from: runs[0].from, to: runs[0].to } else {});

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
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Godot", "cw"),
  name: "Godot",
  description: "Godot-compatible JSON sprite sheet (load at runtime with AtlasTexture/SpriteFrames)",
  extension: ".json",
  icon: "icons/godot-svgrepo-com(1).svg",
  content: std.manifestJsonEx(result, " ") + "\n",
}
