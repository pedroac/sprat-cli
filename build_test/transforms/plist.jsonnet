// plist.jsonnet – Cocos2d-x TextureAtlas plist format (format 2).
local sprat = std.extVar("sprat");

local xml_escape(s) =
  std.strReplace(
    std.strReplace(
      std.strReplace(
        std.strReplace(
          std.strReplace(s, "&", "&amp;"),
          "<", "&lt;"),
        ">", "&gt;"),
      '"', "&quot;"),
    "'", "&apos;");

local sprite_entry(s) =
  local content_w = s.content_w;
  local content_h = s.content_h;
  local source_w  = s.source_w;
  local source_h  = s.source_h;
  local cx = std.floor((s.trim_left - s.trim_right) / 2);
  local cy = std.floor((s.trim_bottom - s.trim_top) / 2);
  local plist_frame = "{" + s.x + "," + s.y + "},{" + s.w + "," + s.h + "}";
  local plist_offset = "{" + cx + "," + cy + "}";
  local plist_source_color_rect = "{" + s.trim_left + "," + s.trim_top + "},{" + content_w + "," + content_h + "}";
  local plist_source_size = "{" + source_w + "," + source_h + "}";
  '\t\t<key>' + xml_escape(s.name) + '</key>\n' +
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
  '\t\t</dict>\n';

local plist_atlas_size = "{" + sprat.atlas_width + "," + sprat.atlas_height + "}";

{
  name: "plist",
  description: "Cocos2d-x TextureAtlas plist format (format 2)",
  extension: ".plist",
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
    '\t\t<string>' + xml_escape(sprat.atlas_path) + '</string>\n' +
    '\t\t<key>size</key>\n' +
    '\t\t<string>' + plist_atlas_size + '</string>\n' +
    '\t\t<key>textureFileName</key>\n' +
    '\t\t<string>' + xml_escape(sprat.atlas_path) + '</string>\n' +
    '\t</dict>\n' +
    '</dict>\n' +
    '</plist>\n',
}
