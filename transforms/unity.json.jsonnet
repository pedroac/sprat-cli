// unity.json.jsonnet – Unity-compatible JSON sprite sheet (TexturePacker JSON Hash format).
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_obj(s) = {
  // Unrotated size; readers swap it themselves when rotated is true.
  frame: { x: s.x, y: s.y, w: s.content_w, h: s.content_h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
} + (if s.has_pivot then { pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm } } else {})
  + (if s.has_slice then {
  border: { l: s.slice_left, t: s.slice_top, r: s.slice_right, b: s.slice_bottom },
} else {});

// One texture's sheet; `at` carries the paths, size and sprites of that atlas.
local render(at) =
  local meta =
    {
      app: "https://github.com/pedroac/sprat-cli",
      version: "1.0",
      image: at.path,
      format: "RGBA8888",
      size: { w: at.width, h: at.height },
      scale: "" + sprat.scale,
    } +
    (if sprat.has_normal_atlas || sprat.has_collision_atlas then {
      secondary_textures:
        (if sprat.has_normal_atlas then [{ name: "_NormalMap", path: at.normal_path }] else []) +
        (if sprat.has_collision_atlas then [{ name: "_CollisionMap", path: at.collision_path }] else []),
    } else {});
  local result = {
    frames: std.foldl(function(acc, s) acc { [s.name]: frame_obj(s) }, at.sprites, {}),
    meta: meta,
  };
  std.manifestJsonEx(result, "  ") + "\n";

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Unity JSON", "cw"),
  name: "Unity JSON",
  description: "Unity-compatible JSON sprite sheet (TexturePacker JSON Hash format with normalized pivots; multipack layouts write one file per atlas and need --output-dir)",
  extension: ".json",
  icon: "icons/unity-svgrepo-com.svg",
} + (
  // A sheet describes one texture: multipack layouts get one file per atlas.
  if lib.is_multi_atlas(sprat) then {
    files: [{ filename: at.stem + ".json", content: render(at) } for at in sprat.atlases],
  } else {
    content: render({
      path: sprat.atlas_path,
      width: sprat.atlas_width,
      height: sprat.atlas_height,
      normal_path: sprat.normal_atlas_path,
      collision_path: sprat.collision_atlas_path,
      sprites: sprat.sprites,
    }),
  }
)
