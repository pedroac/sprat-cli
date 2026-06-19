// aseprite.jsonnet – Aseprite JSON Array sprite sheet format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_obj(s) = {
  filename: s.name,
  frame: { x: s.x, y: s.y, w: s.w, h: s.h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  duration: 100,
};

// Build frameTags from animations: each animation may be non-contiguous, so split into runs.
local frame_tag(anim) =
  local runs = lib.consecutive_runs(anim.frame_indices);
  [
    { name: anim.name, from: run.from, to: run.to, direction: "forward" }
    for run in runs
  ];

local frame_tags = std.flatMap(frame_tag, sprat.animations);

local result = {
  frames: [frame_obj(s) for s in sprat.sprites],
  meta: {
    app: "https://www.aseprite.org/",
    version: "1.3",
    image: sprat.atlas_path,
    format: "RGBA8888",
    size: { w: sprat.atlas_width, h: sprat.atlas_height },
    scale: "" + sprat.scale,
    frameTags: frame_tags,
    layers: [],
    slices: [],
  },
};

{
  name: "Aseprite",
  description: "Aseprite JSON Array sprite sheet format (frameTags populated when animations are present)",
  extension: ".json",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
