// sprat.libsonnet – shared helpers for all sprat Jsonnet transforms.
{
  // Format a double like C's %.8g: up to 8 decimal places, no trailing zeros.
  // Jsonnet v0.20.0 has a known bug with %g format; we use %f + trim instead.
  format_double(v)::
    local s = std.format("%.8f", v);
    local rtrim(str) =
      if std.length(str) == 0 then "0"
      else if str[std.length(str) - 1] == "0" then rtrim(std.substr(str, 0, std.length(str) - 1))
      else if str[std.length(str) - 1] == "." then std.substr(str, 0, std.length(str) - 1)
      else str;
    rtrim(s),

  // Rotated sprites are turned 90 degrees in the atlas, clockwise by default or counter-clockwise
  // for layouts with "rotation ccw". Formats with a fixed convention assert it:
  //   assert lib.rotation_matches(sprat, "cw") : lib.rotation_error("Phaser", "cw"),
  rotation_matches(sprat, direction)::
    !sprat.has_rotated || sprat.rotation_direction == direction,

  rotation_error(format, direction)::
    format + " expects rotated sprites turned "
    + (if direction == "ccw" then "counter-clockwise" else "clockwise")
    + "; re-run spratlayout with --rotate-direction " + direction + " (or without --rotate)",

  // Rotation of a sprite in clockwise degrees: 0, 90 (cw) or 270 (ccw).
  rotation_degrees(sprat, s)::
    if !s.rotated then 0 else if sprat.rotation_direction == "ccw" then 270 else 90,

  // Escape text for XML attribute values and element content.
  xml_escape(s)::
    std.strReplace(
      std.strReplace(
        std.strReplace(
          std.strReplace(
            std.strReplace(s, "&", "&amp;"),
            "<", "&lt;"),
          ">", "&gt;"),
        '"', "&quot;"),
      "'", "&apos;"),

  // Escape text for a single-quoted CSS string, e.g. url('...').
  css_string_escape(s)::
    std.strReplace(
      std.strReplace(
        std.strReplace(
          std.strReplace(s, "\\", "\\\\"),
          "'", "\\'"),
        "\n", "\\a "),
      "\r", ""),

  // Make text safe inside a CSS /* ... */ comment.
  css_comment_escape(s)::
    std.strReplace(s, "*/", "* /"),

  // Split an array of frame indices into contiguous runs.
  // Returns [{from: N, to: M}, ...].
  consecutive_runs(indices)::
    if std.length(indices) == 0 then []
    else
      local fold_result = std.foldl(
        function(acc, idx)
          if acc.current_end == idx - 1 then
            acc { current_end: idx }
          else
            acc {
              runs: acc.runs + [{from: acc.current_start, to: acc.current_end}],
              current_start: idx,
              current_end: idx,
            },
        indices[1:],
        { runs: [], current_start: indices[0], current_end: indices[0] }
      );
      fold_result.runs + [{from: fold_result.current_start, to: fold_result.current_end}],
}
