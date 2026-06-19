// libgdx.jsonnet – LibGDX TextureAtlas format (.atlas).
local sprat = std.extVar("sprat");

local sprite_entry(s) =
  s.name + "\n" +
  "  rotate: " + s.rotated + "\n" +
  "  xy: " + s.x + ", " + s.y + "\n" +
  "  size: " + s.w + ", " + s.h + "\n" +
  "  orig: " + s.source_w + ", " + s.source_h + "\n" +
  "  offset: " + s.trim_left + ", " + s.trim_bottom + "\n" +
  "  index: -1\n";

local atlas_block(at) =
  at.path + "\n" +
  "size: " + at.width + "," + at.height + "\n" +
  "format: RGBA8888\n" +
  "filter: Nearest,Nearest\n" +
  "repeat: none\n\n" +
  std.join("", [sprite_entry(s) for s in at.sprites]);

{
  name: "LibGDX",
  description: "LibGDX TextureAtlas format (.atlas); animation data is not part of this format",
  extension: ".atlas",
  content: std.join("", [atlas_block(at) for at in sprat.atlases]),
}
