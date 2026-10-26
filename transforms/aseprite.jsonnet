// aseprite.jsonnet – Aseprite JSON Array sprite sheet format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

// `frame` is the sprite's position in its own file (local_index), which differs from the
// global sprite index when a multipack layout is split into one file per atlas.
local slice_obj(s, local_index) = {
  name: s.name,
  color: "#0000ffff",
  keys: [{
    frame: local_index,
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
  // Unrotated size; readers swap it themselves when rotated is true.
  frame: { x: s.x, y: s.y, w: s.content_w, h: s.content_h },
  rotated: s.rotated,
  trimmed: s.has_trim,
  spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
  sourceSize: { w: s.source_w, h: s.source_h },
  duration: 100,
};

// Build frameTags from the animation frames held in `sprites`: animations may be
// non-contiguous or span atlases, so each consecutive run becomes its own tag. A tag's
// repeat counts passes, and each direction of a ping-pong is one pass; no repeat loops.
local frame_tags(sprites) =
  local local_index = { ["" + sprites[i].index]: i for i in std.range(0, std.length(sprites) - 1) };
  local tags(anim) =
    local held = [idx for idx in anim.frame_indices if std.objectHas(local_index, "" + idx)];
    [
      { name: anim.name, from: local_index["" + run.from], to: local_index["" + run.to],
        direction: if lib.is_pingpong(anim) then "pingpong" else "forward" }
      + (if lib.loops(anim) then {} else { repeat: if lib.is_pingpong(anim) then "2" else "1" })
      for run in lib.consecutive_runs(held)
    ];
  if std.length(sprites) == 0 then [] else std.flatMap(tags, sprat.animations);

local render(image, width, height, sprites) =
  local result = {
    frames: [frame_obj(s) for s in sprites],
    meta: {
      app: "https://www.aseprite.org/",
      version: "1.3",
      image: image,
      format: "RGBA8888",
      size: { w: width, h: height },
      scale: "" + sprat.scale,
      frameTags: frame_tags(sprites),
      layers: [],
      slices: [
        slice_obj(sprites[i], i)
        for i in std.range(0, std.length(sprites) - 1)
        if sprites[i].has_slice
      ],
    },
  };
  std.manifestJsonEx(result, "  ") + "\n";

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Aseprite", "cw"),
  name: "Aseprite",
  description: "Aseprite JSON Array sprite sheet format (frameTags populated when animations are present; multipack layouts write one file per atlas and need --output-dir)",
  extension: ".json",
  icon: "icons/aseprite-svgrepo-com.svg",
} + (
  // An Aseprite sheet describes one texture: multipack layouts get one file per atlas.
  if lib.is_multi_atlas(sprat) then {
    files: [
      { filename: at.stem + ".json", content: render(at.path, at.width, at.height, at.sprites) }
      for at in sprat.atlases
    ],
  } else {
    content: render(sprat.atlas_path, sprat.atlas_width, sprat.atlas_height, sprat.sprites),
  }
)
