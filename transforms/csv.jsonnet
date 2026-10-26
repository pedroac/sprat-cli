// csv.jsonnet – CSV rows for spreadsheets and data tools.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local csv_escape(s) =
  local needs_quotes = std.length(
    [c for c in std.stringChars(s) if c == '"' || c == ',' || c == '\n' || c == '\r']
  ) > 0;
  if needs_quotes then
    '"' + std.strReplace(s, '"', '""') + '"'
  else
    s;

local marker_vertices_csv(verts) =
  std.join("|", ["" + v.x + "," + v.y for v in verts]);

local marker_json(m) =
  '{"name":' + std.manifestJsonEx(m.name, "") + ',"type":"' + m.type + '"' +
  ',"x":' + m.x + ',"y":' + m.y +
  (if m.type == "circle" then ',"radius":' + m.radius else "") +
  (if m.type == "rectangle" then ',"w":' + m.w + ',"h":' + m.h else "") +
  (if m.type == "polygon" then
    ',"vertices":[' + std.join(",", ['{"x":' + v.x + ',"y":' + v.y + '}' for v in m.vertices]) + ']'
   else "") +
  "}";

local markers_json_array(markers) =
  "[" + std.join(",", [marker_json(m) for m in markers]) + "]";

local header = "index,name,path,atlas_index,atlas_path,x,y,w,h,pivot_x,pivot_y,trim_left,trim_top,trim_right,trim_bottom,marker_count,markers_json,rotation,has_slice,slice_left,slice_top,slice_right,slice_bottom,slice_h,slice_v\n";

local sprite_row(s) =
  local has_slice = s.has_slice;
  "" + s.index + "," +
  csv_escape(s.name) + "," +
  csv_escape(s.path) + "," +
  s.atlas_index + "," +
  csv_escape(s.atlas_path) + "," +
  s.x + "," + s.y + "," + s.w + "," + s.h + "," +
  s.pivot_x + "," + s.pivot_y + "," +
  s.trim_left + "," + s.trim_top + "," + s.trim_right + "," + s.trim_bottom + "," +
  std.length(s.markers) + "," +
  csv_escape(markers_json_array(s.markers)) + "," +
  lib.rotation_degrees(sprat, s) + "," +
  (if has_slice then "true" else "false") + "," +
  (if has_slice then "" + s.slice_left else "") + "," +
  (if has_slice then "" + s.slice_top else "") + "," +
  (if has_slice then "" + s.slice_right else "") + "," +
  (if has_slice then "" + s.slice_bottom else "") + "," +
  (if has_slice then s.slice_h else "") + "," +
  (if has_slice then s.slice_v else "") + "\n";

// Markers only carry the fields of their type; the other columns stay empty.
local field_or_empty(m, k) = if std.objectHas(m, k) then "" + m[k] else "";

local marker_row(m) =
  "marker," + m.index + "," +
  csv_escape(m.name) + "," +
  m.type + "," +
  m.x + "," + m.y + "," +
  field_or_empty(m, "radius") + "," + field_or_empty(m, "w") + "," + field_or_empty(m, "h") + "," +
  csv_escape(if std.objectHas(m, "vertices") then marker_vertices_csv(m.vertices) else "") + "," +
  m.sprite_index + "," +
  csv_escape(m.sprite_name) + "," +
  csv_escape(m.sprite_path) + "\n";

local anim_row(a) =
  if a.is_alias then
    "animation," + a.index + "," + csv_escape(a.name) + ",alias," +
    csv_escape(a.alias_source) +
    (if a.flip != "" then "," + a.flip else "") + "\n"
  else
    "animation," + a.index + "," + csv_escape(a.name) + "," + a.fps + "," +
    std.join("|", ["" + idx for idx in a.frame_indices]) + "," + a.flip + "," + a.loop + "\n";

local body =
  std.join("", [sprite_row(s) for s in sprat.sprites]) +
  std.join("", [marker_row(m) for m in sprat.markers]) +
  std.join("", [anim_row(a) for a in sprat.animations]);

{
  name: "CSV",
  description: "CSV rows for spreadsheets and data tools",
  extension: ".csv",
  icon: "icons/csv-svgrepo-com.svg",
  content: header + body,
}
