#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: output_pattern_test.sh <spratpack-bin> <spratconvert-bin>" >&2
    exit 1
fi

spratpack_bin="$1"
spratconvert_bin="$2"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

layout_single="$tmp_dir/layout_single.txt"
cat > "$layout_single" <<'LAYOUT'
atlas 8,8
scale 1
LAYOUT

layout_multi="$tmp_dir/layout_multi.txt"
cat > "$layout_multi" <<'LAYOUT'
atlas 8,8
atlas 8,8
scale 1
LAYOUT

# spratconvert: valid integer substitution and escaped percent.
"$spratconvert_bin" --transform json --output 'atlas_%d%%.png' < "$layout_multi" > "$tmp_dir/convert_valid.json"
grep -q '"path": "atlas_0%.png"' "$tmp_dir/convert_valid.json"
grep -q '"path": "atlas_1%.png"' "$tmp_dir/convert_valid.json"

# spratconvert: reject unsupported placeholders.
if "$spratconvert_bin" --transform json --output 'atlas_%s.png' < "$layout_multi" > /dev/null 2> "$tmp_dir/convert_bad_spec.err"; then
    echo "spratconvert accepted unsupported output placeholder %s" >&2
    exit 1
fi
grep -q "Invalid output pattern" "$tmp_dir/convert_bad_spec.err"

# spratconvert: reject missing %d for multi-atlas layouts.
if "$spratconvert_bin" --transform json --output 'atlas.png' < "$layout_multi" > /dev/null 2> "$tmp_dir/convert_no_placeholder.err"; then
    echo "spratconvert accepted output pattern without %d for multi-atlas layout" >&2
    exit 1
fi
grep -q "must include %d" "$tmp_dir/convert_no_placeholder.err"

# spratconvert: single atlas can use a literal filename.
"$spratconvert_bin" --transform json --output 'atlas.png' < "$layout_single" > "$tmp_dir/convert_single.json"
grep -q '"path": "atlas.png"' "$tmp_dir/convert_single.json"

# spratpack: valid integer substitution writes both atlas files.
(
    cd "$tmp_dir"
    "$spratpack_bin" --output 'atlas_%d.png' < "$layout_multi"
)
test -f "$tmp_dir/atlas_0.png"
test -f "$tmp_dir/atlas_1.png"

# spratpack: escaped percent is supported.
(
    cd "$tmp_dir"
    "$spratpack_bin" --output 'atlas_%d%%.png' < "$layout_single"
)
test -f "$tmp_dir/atlas_0%.png"

# spratpack: reject unsupported placeholders.
if (
    cd "$tmp_dir"
    "$spratpack_bin" --output 'atlas_%s.png' < "$layout_single"
) > /dev/null 2> "$tmp_dir/pack_bad_spec.err"; then
    echo "spratpack accepted unsupported output placeholder %s" >&2
    exit 1
fi
grep -q "Invalid output pattern" "$tmp_dir/pack_bad_spec.err"

# spratpack: reject missing %d for multi-atlas outputs.
if (
    cd "$tmp_dir"
    "$spratpack_bin" --output 'atlas.png' < "$layout_multi"
) > /dev/null 2> "$tmp_dir/pack_no_placeholder.err"; then
    echo "spratpack accepted output pattern without %d for multi-atlas layout" >&2
    exit 1
fi
grep -q "must include %d" "$tmp_dir/pack_no_placeholder.err"

# spratpack: reject trailing %.
if (
    cd "$tmp_dir"
    "$spratpack_bin" --output 'atlas_%' < "$layout_single"
) > /dev/null 2> "$tmp_dir/pack_trailing_percent.err"; then
    echo "spratpack accepted trailing % in output pattern" >&2
    exit 1
fi
grep -q "trailing '%'" "$tmp_dir/pack_trailing_percent.err"

echo "output_pattern_test.sh: ok"
