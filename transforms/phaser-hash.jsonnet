// phaser-hash.jsonnet – Phaser 3 JSON Hash atlas format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_obj(s) = {
  // Unrotated size; readers swap it themselves when rotated is true.
  frame: { x: s.x, y: s.y, w: s.content_w, h: s.content_h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw },
} + (if s.has_slice then {
  border: { l: s.slice_left, t: s.slice_top, r: s.slice_right, b: s.slice_bottom },
} else {});

local frames_obj = std.foldl(
  function(acc, s) acc { [s.name]: frame_obj(s) },
  sprat.sprites,
  {}
);

local result = {
  frames: frames_obj,
  meta: {
    image: sprat.atlas_path,
    format: "RGBA8888",
    size: { w: sprat.atlas_width, h: sprat.atlas_height },
    scale: "" + sprat.scale,
  },
};

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Phaser", "cw"),
  name: "Phaser JSON Hash",
  description: "Phaser 3 atlas format (JSON Hash, compatible with TexturePacker JSON Hash output)",
  extension: ".json",
  icon: "icons/phaser-logo.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
