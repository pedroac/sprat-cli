// css.jsonnet – CSS classes for web sprite rendering.
local sprat = std.extVar("sprat");

local sprite_css(s) =
  '.sprite-' + s.name_css + ' {\n' +
  (if s.atlas_path != "" then '  background-image: url(\'' + s.atlas_path + '\');\n' else "") +
  '  background-position: -' + s.x + 'px -' + s.y + 'px;\n' +
  '  width: ' + s.w + 'px;\n' +
  '  height: ' + s.h + 'px;\n' +
  '  /* source: ' + s.path + ' */\n' +
  '  /* name: ' + s.name + ' */\n' +
  '  /* atlas_index: ' + s.atlas_index + ' */\n' +
  (if s.rotated then
    '  transform: rotate(-90deg) translate(-100%, 0);\n  transform-origin: top left;\n'
   else "") +
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
  content: header + std.join("\n", [sprite_css(s) for s in sprat.sprites]),
}
