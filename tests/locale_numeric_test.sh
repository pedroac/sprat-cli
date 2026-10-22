#!/usr/bin/env bash
# Regression: a user locale with ',' decimals (pt_PT, de_DE, ...) must not change how the
# tools read or write numbers: option values, layout "scale" lines and the metadata that
# spratconvert hands to its transforms always use '.'.
# Exits 77 (skipped) when no such locale is available.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: locale_numeric_test.sh <spratlayout-bin> <spratpack-bin> <spratconvert-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
spratpack_bin="$2"
spratconvert_bin="$3"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
frames_dir="$script_dir/assets/Crocodile Mascot Running Game/transparent PNG/run"

# Windows programs take their locale from the system settings, not from LC_ALL.
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    echo "locale_numeric test skipped: locales cannot be selected through the environment"
    exit 77
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

mkdir -p "$tmp_dir/cache"
export TMPDIR="$tmp_dir/cache"
export HOME="$tmp_dir"
export XDG_CONFIG_HOME="$tmp_dir/.config"

# Checked with an external printf: it sees LOCPATH, and the shell's own locale stays as is.
uses_decimal_comma() {
    [ "$(env LC_ALL="$1" printf '%.1f' 0.5 2>/dev/null)" = "0,5" ]
}

comma_locale=""
for candidate in pt_PT.UTF-8 de_DE.UTF-8 fr_FR.UTF-8 es_ES.UTF-8 it_IT.UTF-8 nl_NL.UTF-8; do
    if uses_decimal_comma "$candidate"; then
        comma_locale="$candidate"
        break
    fi
done
# glibc systems often ship only C/en_US: build one into a private LOCPATH.
if [ -z "$comma_locale" ] && command -v localedef >/dev/null 2>&1; then
    mkdir -p "$tmp_dir/locale"
    if localedef -i pt_PT -f UTF-8 "$tmp_dir/locale/pt_PT.UTF-8" >/dev/null 2>&1; then
        export LOCPATH="$tmp_dir/locale"
        if uses_decimal_comma pt_PT.UTF-8; then
            comma_locale="pt_PT.UTF-8"
        fi
    fi
fi
if [ -z "$comma_locale" ]; then
    echo "locale_numeric test skipped: no locale with ',' decimals is available"
    exit 77
fi

fail() {
    echo "FAIL ($comma_locale): $*" >&2
    exit 1
}

export LC_ALL="$comma_locale"
export LANG="$comma_locale"

"$spratlayout_bin" "$frames_dir" --scale 0.5 > "$tmp_dir/layout.txt" 2> "$tmp_dir/layout.err" \
    || { cat "$tmp_dir/layout.err" >&2; fail "spratlayout rejected --scale 0.5"; }
grep -qx 'scale 0.5' "$tmp_dir/layout.txt" || fail "layout does not contain 'scale 0.5'"

"$spratpack_bin" < "$tmp_dir/layout.txt" > "$tmp_dir/atlas.png" 2> "$tmp_dir/pack.err" \
    || { cat "$tmp_dir/pack.err" >&2; fail "spratpack rejected the scaled layout"; }
[ -s "$tmp_dir/atlas.png" ] || fail "spratpack wrote an empty atlas"

# Animations add fractional durations, pivots add fractional coordinates.
"$spratconvert_bin" --transform json --auto-animations < "$tmp_dir/layout.txt" \
    > "$tmp_dir/layout.json" 2> "$tmp_dir/convert.err" \
    || { cat "$tmp_dir/convert.err" >&2; fail "spratconvert failed"; }
grep -q '"scale": 0.5' "$tmp_dir/layout.json" || fail "JSON metadata does not contain \"scale\": 0.5"
if grep -qE '[0-9],[0-9]' "$tmp_dir/layout.json"; then
    fail "JSON metadata contains a ',' decimal"
fi

echo "locale_numeric test passed ($comma_locale)"
