// aseprite.jsonnet – Aseprite JSON Array sprite sheet format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local slice_obj(s) = {
  name: s.name,
  color: "#0000ffff",
  keys: [{
    frame: s.index,
    bounds: { x: 0, y: 0, w: s.source_w, h: s.source_h },
    center: {
      x: s.slice_left,
      y: s.slice_top,
      w: s.source_w - s.slice_left - s.slice_right,
      h: s.source_h - s.slice_top - s.slice_bottom,
    },
  }],
};

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
    slices: [slice_obj(s) for s in sprat.sprites if s.has_slice],
  },
};

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Aseprite", "cw"),
  name: "Aseprite",
  description: "Aseprite JSON Array sprite sheet format (frameTags populated when animations are present)",
  extension: ".json",
  icon: "icons/aseprite-svgrepo-com.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
