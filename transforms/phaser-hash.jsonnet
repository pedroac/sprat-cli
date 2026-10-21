// phaser-hash.jsonnet – Phaser 3 JSON Hash atlas format.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frames_obj = std.foldl(
  function(acc, s) acc { [s.name]: lib.phaser_frame(s) },
  sprat.sprites,
  {}
);

local result =
  if lib.is_multi_atlas(sprat) then lib.phaser_multiatlas(sprat)
  else {
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
  description: "Phaser 3 atlas format (JSON Hash, compatible with TexturePacker JSON Hash output; multipack layouts become a multi-atlas JSON for this.load.multiatlas)",
  extension: ".json",
  icon: "icons/phaser-logo.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
