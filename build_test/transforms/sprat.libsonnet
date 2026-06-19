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
