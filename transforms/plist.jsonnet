// plist.jsonnet – Cocos2d-x TextureAtlas plist format (format 2).
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local sprite_entry(s) =
  local content_w = s.content_w;
  local content_h = s.content_h;
  local source_w  = s.source_w;
  local source_h  = s.source_h;
  // Offset of the trimmed rect's center from the source center, Y-up; may be a half pixel.
  local cx = (s.trim_left - s.trim_right) / 2;
  local cy = (s.trim_bottom - s.trim_top) / 2;
  // Cocos2d-x rects are "{{x,y},{w,h}}" with the unrotated size, even for rotated frames.
  local plist_frame = "{{" + s.x + "," + s.y + "},{" + content_w + "," + content_h + "}}";
  local plist_offset = "{" + cx + "," + cy + "}";
  local plist_source_color_rect = "{{" + s.trim_left + "," + s.trim_top + "},{" + content_w + "," + content_h + "}}";
  local plist_source_size = "{" + source_w + "," + source_h + "}";
  '\t\t<key>' + lib.xml_escape(s.name) + '</key>\n' +
  '\t\t<dict>\n' +
  '\t\t\t<key>frame</key>\n' +
  '\t\t\t<string>' + plist_frame + '</string>\n' +
  '\t\t\t<key>offset</key>\n' +
  '\t\t\t<string>' + plist_offset + '</string>\n' +
  '\t\t\t<key>rotated</key>\n' +
  '\t\t\t' + (if s.rotated then '<true/>' else '<false/>') + '\n' +
  '\t\t\t<key>sourceColorRect</key>\n' +
  '\t\t\t<string>' + plist_source_color_rect + '</string>\n' +
  '\t\t\t<key>sourceSize</key>\n' +
  '\t\t\t<string>' + plist_source_size + '</string>\n' +
  (if s.has_slice then
    '\t\t\t<key>capInsets</key>\n' +
    '\t\t\t<string>{{' + s.slice_left + ',' + s.slice_top + '},{' +
    (source_w - s.slice_left - s.slice_right) + ',' +
    (source_h - s.slice_top - s.slice_bottom) + '}}</string>\n'
  else "") +
  '\t\t</dict>\n';

local plist_atlas_size = "{" + sprat.atlas_width + "," + sprat.atlas_height + "}";

{
  assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Cocos2d-x plist", "cw"),
  name: "plist",
  description: "Cocos2d-x TextureAtlas plist format (format 2)",
  extension: ".plist",
  icon: "icons/plist.svg",
  content:
    '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n' +
    '<plist version="1.0">\n' +
    '<dict>\n' +
    '\t<key>frames</key>\n' +
    '\t<dict>\n\n' +
    std.join("", [sprite_entry(s) for s in sprat.sprites]) +
    '\t</dict>\n' +
    '\t<key>metadata</key>\n' +
    '\t<dict>\n' +
    '\t\t<key>format</key>\n' +
    '\t\t<integer>2</integer>\n' +
    '\t\t<key>realTextureFileName</key>\n' +
    '\t\t<string>' + lib.xml_escape(sprat.atlas_path) + '</string>\n' +
    '\t\t<key>size</key>\n' +
    '\t\t<string>' + plist_atlas_size + '</string>\n' +
    '\t\t<key>textureFileName</key>\n' +
    '\t\t<string>' + lib.xml_escape(sprat.atlas_path) + '</string>\n' +
    '\t</dict>\n' +
    '</dict>\n' +
    '</plist>\n',
}
