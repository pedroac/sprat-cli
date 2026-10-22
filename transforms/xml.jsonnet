// xml.jsonnet – XML layout format for engine import pipelines.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local marker_xml(m) =
  if m.type == "point" then
    '<marker name="' + lib.xml_escape(m.name) + '" type="point" x="' + m.x + '" y="' + m.y + '" />'
  else if m.type == "circle" then
    '<marker name="' + lib.xml_escape(m.name) + '" type="circle" x="' + m.x + '" y="' + m.y + '" radius="' + m.radius + '" />'
  else if m.type == "rectangle" then
    '<marker name="' + lib.xml_escape(m.name) + '" type="rectangle" x="' + m.x + '" y="' + m.y + '" w="' + m.w + '" h="' + m.h + '" />'
  else if m.type == "polygon" then
    '<marker name="' + lib.xml_escape(m.name) + '" type="polygon"><vertices>' +
    std.join("|", ["" + v.x + "," + v.y for v in m.vertices]) +
    '</vertices></marker>'
  else "";

local sprite_xml(s) =
  local marker_section =
    if std.length(s.markers) > 0 then
      "\n  <markers>\n    " +
      std.join("\n    ", [marker_xml(m) for m in s.markers]) +
      "\n  </markers>\n"
    else "";
  local slice_attrs =
    if s.has_slice then
      ' slice_left="' + s.slice_left + '" slice_top="' + s.slice_top +
      '" slice_right="' + s.slice_right + '" slice_bottom="' + s.slice_bottom +
      '" slice_h="' + s.slice_h + '" slice_v="' + s.slice_v + '"'
    else "";
  '<sprite index="' + s.index + '" name="' + lib.xml_escape(s.name) + '" path="' + lib.xml_escape(s.path) +
  '" x="' + s.x + '" y="' + s.y + '" w="' + s.w + '" h="' + s.h +
  '" pivot_x="' + s.pivot_x + '" pivot_y="' + s.pivot_y +
  '" trim_left="' + s.trim_left + '" trim_top="' + s.trim_top +
  '" trim_right="' + s.trim_right + '" trim_bottom="' + s.trim_bottom +
  '" marker_count="' + std.length(s.markers) + '" rotation="' + lib.rotation_degrees(sprat, s) + '"' +
  slice_attrs + '>' +
  marker_section + "</sprite>";

local atlas_xml(at) =
  '    <atlas index="' + at.index + '" width="' + at.width + '" height="' + at.height +
  '" path="' + lib.xml_escape(at.path) + '">\n' +
  '      <sprites>\n        ' +
  std.join("\n        ", [sprite_xml(s) for s in at.sprites]) +
  '\n      </sprites>\n    </atlas>';

local anim_xml(a) =
  if a.is_alias then
    '    <animation index="' + a.index + '" name="' + lib.xml_escape(a.name) +
    '" alias="' + lib.xml_escape(a.alias_source) + '"' +
    (if a.flip != "" then ' flip="' + a.flip + '"' else "") + " />"
  else
    '    <animation index="' + a.index + '" name="' + lib.xml_escape(a.name) +
    '" fps="' + a.fps + '" sprite_indexes="' +
    std.join(",", ["" + idx for idx in a.frame_indices]) + '"' +
    (if a.flip != "" then ' flip="' + a.flip + '"' else "") + " />";

local atlases_section =
  '  <atlases>\n' +
  std.join("\n", [atlas_xml(at) for at in sprat.atlases]) +
  '\n  </atlases>\n';

local animations_section =
  if sprat.has_animations then
    '  <animations>\n' +
    std.join("\n", [anim_xml(a) for a in sprat.animations]) +
    '\n  </animations>\n'
  else "";

{
  name: "XML",
  description: "XML layout format for engine import pipelines",
  extension: ".xml",
  icon: "icons/xml-svgrepo-com.svg",
  content:
    '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<layout multipack="' + sprat.multipack + '" scale="' + sprat.scale + '" extrude="' + sprat.extrude + '">\n' +
    atlases_section +
    animations_section +
    '</layout>\n',
}
