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

  // Multipack layouts span several atlases. Formats that describe a single texture then
  // write one file per atlas (via `files`), or switch to their multi-texture variant.
  is_multi_atlas(sprat)::
    sprat.atlas_count > 1,

  // Unity asset GUID of an atlas texture, the same in every output of a run so .anim clips
  // find the texture their .meta describes. A single atlas hashes the --atlas pattern
  // (constant without one; pass --atlas to keep atlases of one project apart); multipack
  // atlases each hash their own path.
  unity_guid(sprat, atlas_index)::
    (if sprat.atlas_count > 1 then sprat.atlases[atlas_index].hash_hex else sprat.output_pattern_hash_hex)
    + "0000000000000000",

  // Phaser 3 frame (TexturePacker JSON style); the frame size is unrotated, readers swap it.
  phaser_frame(s):: {
    frame: { x: s.x, y: s.y, w: s.content_w, h: s.content_h },
    rotated: s.rotated,
    trimmed: s.has_trim,
    spriteSourceSize: { x: s.trim_left, y: s.trim_top, w: s.content_w, h: s.content_h },
    sourceSize: { w: s.source_w, h: s.source_h },
  }
  // Phaser applies any pivot as the sprite's origin, so only write one that was set and
  // leave Phaser's own default otherwise.
  + (if s.has_pivot then { pivot: { x: s.pivot_x_norm, y: s.pivot_y_norm_raw } } else {})
  + (if s.has_slice then {
    border: { l: s.slice_left, t: s.slice_top, r: s.slice_right, b: s.slice_bottom },
  } else {}),

  // Phaser 3 multi-atlas JSON (this.load.multiatlas): one entry per atlas texture, all
  // frames under a single texture key.
  phaser_multiatlas(sprat):: {
    textures: [
      {
        image: at.path,
        format: "RGBA8888",
        size: { w: at.width, h: at.height },
        scale: sprat.scale,
        frames: [{ filename: s.name } + $.phaser_frame(s) for s in at.sprites],
      }
      for at in sprat.atlases
    ],
    meta: {
      app: "https://github.com/pedroac/sprat-cli",
      format: "RGBA8888",
      scale: "" + sprat.scale,
    },
  },

  // Turn a name (e.g. an animation name) into a plain file name: path separators, characters
  // invalid on Windows and a leading dot (hidden files, which Unity skips) become "_", a
  // trailing dot or space (dropped by Windows) too, and Windows device names such as "con"
  // or "com1" get a "_" prefix. spratconvert refuses unsafe names anyway.
  safe_filename(s)::
    local chars = std.stringChars(s);
    local n = std.length(chars);
    local cleaned = std.join("", [
      local c = chars[i];
      if std.codepoint(c) < 32 || std.member('/\\:*?"<>|', c) || (i == 0 && c == ".")
         || (i == n - 1 && (c == "." || c == " ")) then "_" else c
      for i in std.range(0, n - 1)
    ]);
    local stem = std.asciiUpper(std.split(cleaned, ".")[0]);
    local reserved = std.member(["CON", "PRN", "AUX", "NUL"], stem)
      || (std.length(stem) == 4 && std.member(["COM", "LPT"], std.substr(stem, 0, 3))
          && std.member("123456789", std.substr(stem, 3, 1)));
    if cleaned == "" then "_" else if reserved then "_" + cleaned else cleaned,

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

  // Emit text as a YAML scalar: plain when that is unambiguous, otherwise double-quoted.
  // A JSON string literal is a valid YAML double-quoted scalar.
  yaml_string(s)::
    local chars = std.stringChars(s);
    local needs_quotes =
      s == ""
      || std.member("-?:,[]{}#&*!|>'\"%@`", chars[0])
      || chars[0] == " " || chars[std.length(chars) - 1] == " "
      || chars[std.length(chars) - 1] == ":"
      || std.length(std.findSubstr(": ", s)) > 0
      || std.length(std.findSubstr(" #", s)) > 0
      || std.length([c for c in chars if std.codepoint(c) < 32 || c == "\\"]) > 0;
    if needs_quotes then std.escapeStringJson(s) else s,

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
