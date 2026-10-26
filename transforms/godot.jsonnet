// godot.jsonnet – Godot-compatible JSON sprite sheet.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local multi = lib.is_multi_atlas(sprat);

local frame_obj(s) = {
  name: s.name,
  region: { x: s.x, y: s.y, w: s.w, h: s.h },
  margin: { left: s.trim_left, top: s.trim_top, right: s.trim_right, bottom: s.trim_bottom },
  source_size: { w: s.source_w, h: s.source_h },
  rotated: s.rotated,
  pivot_offset: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw },
} + (if multi then { atlas: s.atlas_index } else {})
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

// frames lists every frame in play order (indices into frames[]); from/to are only
// emitted when the frames form one ascending run, so they never misdescribe the animation.
// SpriteFrames has only a loop flag, so a ping-pong animation lists its way back too.
local anim_obj(a) =
  local frames = lib.play_order(a.frame_indices, a);
  local runs = lib.consecutive_runs(frames);
  {
    name: a.name,
    frames: frames,
    speed: a.fps,
    loop: lib.loops(a),
  } + (if std.length(runs) == 1 then { from: runs[0].from, to: runs[0].to } else {});

// Texture fields of one atlas; multipack layouts list every atlas and tag frames with theirs.
local atlas_obj(image, w, h, normal_image, collision_image) =
  { image: image, size: { w: w, h: h } } +
  (if sprat.has_normal_atlas then { normal_image: normal_image } else {}) +
  (if sprat.has_collision_atlas then { collision_image: collision_image } else {});

local result =
  (if multi then {
    atlases: [atlas_obj(at.path, at.width, at.height, at.normal_path, at.collision_path) for at in sprat.atlases],
  } else atlas_obj(sprat.atlas_path, sprat.atlas_width, sprat.atlas_height,
                   sprat.normal_atlas_path, sprat.collision_atlas_path)) +
  {
    scale: sprat.scale,
    frames: [frame_obj(s) for s in sprat.sprites],
  } +
  (if sprat.has_collision_atlas then { collision_scale: sprat.collision_atlas_scale } else {}) +
  (if sprat.has_animations then {
    animations: [anim_obj(a) for a in sprat.animations],
  } else {});

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Godot", "cw"),
  name: "Godot",
  description: "Godot-compatible JSON sprite sheet (load at runtime with AtlasTexture/SpriteFrames; multipack layouts list atlases and give each frame its atlas index)",
  extension: ".json",
  icon: "icons/godot-svgrepo-com(1).svg",
  content: std.manifestJsonEx(result, " ") + "\n",
}
