// css.jsonnet – CSS classes for web sprite rendering.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local sprite_css(s) =
  '.sprite-' + s.name_css + ' {\n' +
  (if s.atlas_path != "" then '  background-image: url(\'' + lib.css_string_escape(s.atlas_path) + '\');\n' else "") +
  '  background-position: -' + s.x + 'px -' + s.y + 'px;\n' +
  '  width: ' + s.w + 'px;\n' +
  '  height: ' + s.h + 'px;\n' +
  '  /* source: ' + lib.css_comment_escape(s.path) + ' */\n' +
  '  /* name: ' + lib.css_comment_escape(s.name) + ' */\n' +
  '  /* atlas_index: ' + s.atlas_index + ' */\n' +
  // Undo the packing rotation: clockwise-packed sprites turn back -90deg, counter-clockwise +90deg.
  (if !s.rotated then ""
   else if sprat.rotation_direction == "ccw" then
    '  transform: rotate(90deg) translate(0, -100%);\n  transform-origin: top left;\n'
   else
    '  transform: rotate(-90deg) translate(-100%, 0);\n  transform-origin: top left;\n') +
  '}\n';

local header =
  ':root {\n' +
  '  --atlas-width: ' + sprat.atlas_width + 'px;\n' +
  '  --atlas-height: ' + sprat.atlas_height + 'px;\n' +
  '  --atlas-scale: ' + sprat.scale + ';\n' +
  '}\n\n' +
  '.sprat-sprite {\n' +
  '  background-repeat: no-repeat;\n' +
  '  display: inline-block;\n' +
  '}\n';

{
  name: "CSS",
  description: "CSS classes for web sprite rendering",
  extension: ".css",
  icon: "icons/css-3-svgrepo-com.svg",
  content: header + std.join("\n", [sprite_css(s) for s in sprat.sprites]),
}
