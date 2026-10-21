// unity.json.jsonnet – Unity-compatible JSON sprite sheet (TexturePacker JSON Hash format).
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_obj(s) = {
  frame: { x: s.x, y: s.y, w: s.w, h: s.h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm },
} + (if s.has_slice then {
  border: { l: s.slice_left, t: s.slice_top, r: s.slice_right, b: s.slice_bottom },
} else {});

local frames_obj = std.foldl(
  function(acc, s) acc { [s.name]: frame_obj(s) },
  sprat.sprites,
  {}
);

local meta =
  {
    app: "https://github.com/pedroac/sprat-cli",
    version: "1.0",
    image: sprat.atlas_path,
    format: "RGBA8888",
    size: { w: sprat.atlas_width, h: sprat.atlas_height },
    scale: "" + sprat.scale,
  } +
  (if sprat.has_normal_atlas || sprat.has_collision_atlas then {
    secondary_textures:
      (if sprat.has_normal_atlas then [{ name: "_NormalMap", path: sprat.normal_atlas_path }] else []) +
      (if sprat.has_collision_atlas then [{ name: "_CollisionMap", path: sprat.collision_atlas_path }] else []),
  } else {});

local result = {
  frames: frames_obj,
  meta: meta,
};

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Unity JSON", "cw"),
  name: "Unity JSON",
  description: "Unity-compatible JSON sprite sheet (TexturePacker JSON Hash format with normalized pivots)",
  extension: ".json",
  icon: "icons/unity-svgrepo-com.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
