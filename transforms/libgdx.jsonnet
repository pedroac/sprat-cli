// libgdx.jsonnet – LibGDX TextureAtlas format (.atlas).
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local sprite_entry(s) =
  s.name + "\n" +
  "  rotate: " + s.rotated + "\n" +
  "  xy: " + s.x + ", " + s.y + "\n" +
  // libGDX expects the unrotated region size and swaps it itself when rotate is true.
  "  size: " + (if s.rotated then s.h + ", " + s.w else s.w + ", " + s.h) + "\n" +
  "  orig: " + s.source_w + ", " + s.source_h + "\n" +
  "  offset: " + s.trim_left + ", " + s.trim_bottom + "\n" +
  "  index: -1\n" +
  (if s.has_slice then
    "  split: " + s.slice_left + ", " + s.slice_right + ", " + s.slice_top + ", " + s.slice_bottom + "\n"
  else "");

local atlas_block(at) =
  at.path + "\n" +
  "size: " + at.width + "," + at.height + "\n" +
  "format: RGBA8888\n" +
  "filter: Nearest,Nearest\n" +
  "repeat: none\n\n" +
  std.join("", [sprite_entry(s) for s in at.sprites]);

{
  assert lib.rotation_matches(sprat, "ccw") : lib.rotation_error("libGDX", "ccw"),
  name: "LibGDX",
  description: "LibGDX TextureAtlas format (.atlas); rotated sprites need spratlayout --rotate-direction ccw; animation data is not part of this format",
  extension: ".atlas",
  icon: "icons/libgdx.svg",
  // libGDX starts a new page only after a blank line.
  content: std.join("\n", [atlas_block(at) for at in sprat.atlases]),
}
