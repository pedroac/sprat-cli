// phaser-anims.jsonnet – Phaser 3 animation manager JSON.
local sprat = std.extVar("sprat");

local frame_ref(f) = { key: sprat.atlas_stem, frame: f.name };

local anim_obj(a) = {
  key: a.name,
  frameRate: a.fps,
  repeat: -1,
  frames: [frame_ref(f) for f in a.frames],
};

local result = {
  anims: [anim_obj(a) for a in sprat.animations],
};

{
  name: "Phaser Animations",
  description: "Phaser 3 animation manager JSON (load separately via this.anims.fromJSON(); requires --atlas so frame keys resolve to the correct texture)",
  extension: ".json",
  content: std.manifestJsonEx(result, "  ") + "\n",
}
