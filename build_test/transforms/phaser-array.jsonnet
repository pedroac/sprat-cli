// phaser-array.jsonnet – Phaser 3 JSON Array atlas format.
local sprat = std.extVar("sprat");

local frame_obj(s) = {
  filename: s.name,
  frame: { x: s.x, y: s.y, w: s.w, h: s.h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw },
};

local result = {
  frames: [frame_obj(s) for s in sprat.sprites],
  meta: {
    image: sprat.atlas_path,
    format: "RGBA8888",
    size: { w: sprat.atlas_width, h: sprat.atlas_height },
    scale: "" + sprat.scale,
  },
};

{
  name: "Phaser JSON Array",
  description: "Phaser 3 atlas format (JSON Array, compatible with TexturePacker JSON Array output)",
  extension: ".json",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
