// phaser-anims.jsonnet – Phaser 3 animation manager JSON.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

// texture_key is the atlas stem, or for multipack the shared multi-atlas key ("atlas" for
// atlas_%d.png), matching the key the texture is usually loaded under.
local frame_ref(f) = { key: sprat.texture_key, frame: f.name };

local anim_obj(a) = {
  key: a.name,
  frameRate: a.fps,
  repeat: if lib.loops(a) then -1 else 0,
  frames: [frame_ref(f) for f in a.frames],
} + (if lib.is_pingpong(a) then { yoyo: true } else {});

local result = {
  anims: [anim_obj(a) for a in sprat.animations],
};

{
  name: "Phaser Animations",
  description: "Phaser 3 animation manager JSON (load separately via this.anims.fromJSON(); frame keys are the atlas stem, or for multipack the -a pattern's stem without %d)",
  extension: ".json",
  icon: "icons/phaser-logo.svg",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
