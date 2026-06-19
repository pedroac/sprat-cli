// phaser-hash.jsonnet – Phaser 3 JSON Hash atlas format.
local sprat = std.extVar("sprat");

local frame_obj(s) = {
  frame: { x: s.x, y: s.y, w: s.w, h: s.h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw },
};

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
  name: "Phaser JSON Hash",
  description: "Phaser 3 atlas format (JSON Hash, compatible with TexturePacker JSON Hash output)",
  extension: ".json",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
