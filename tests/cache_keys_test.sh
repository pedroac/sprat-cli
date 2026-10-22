#!/usr/bin/env bash
# Regression: spratlayout's caches must never return a layout computed for other options.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: cache_keys_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
props_dir="$script_dir/assets/Streets of Fight files/Stage Layers/props"
profiles_src="$script_dir/../spratprofiles.cfg"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# Private cache location so earlier runs (or the user's cache) cannot affect results.
mkdir -p "$tmp_dir/cache"
TMPDIR="$(fix_path "$tmp_dir/cache")"
TMP="$TMPDIR"
TEMP="$TMPDIR"
export TMPDIR TMP TEMP

profiles_cfg="$(fix_path "$profiles_src")"

# The cache is keyed by folder, so each scenario gets its own copy of the sprites.
fresh_copy() {
    local dir="$tmp_dir/$1"
    mkdir -p "$dir"
    find "$props_dir" -name '*.png' -exec cp {} "$dir/" \;
    fix_path "$dir"
}

# Layout without the root line, which differs between copies.
layout_body() {
    "$spratlayout_bin" "$@" | tr -d '\r' | grep -v '^root '
}

echo "Test 1: --multipack after a cached plain run"
dir="$(fresh_copy multipack)"
layout_body "$dir" > /dev/null
if ! layout_body "$dir" --multipack | grep -q '^multipack true$'; then
    echo "FAILED: --multipack returned a cached layout without 'multipack true'" >&2
    exit 1
fi

echo "Test 2: --sort metric after a cached run with another metric"
warm_dir="$(fresh_copy sort_warm)"
cold_dir="$(fresh_copy sort_cold)"
layout_body "$warm_dir" --sort stable:area > /dev/null
layout_body "$warm_dir" --sort stable:height > "$tmp_dir/sort_warm.txt"
layout_body "$cold_dir" --sort stable:height > "$tmp_dir/sort_cold.txt"
if ! diff -u "$tmp_dir/sort_cold.txt" "$tmp_dir/sort_warm.txt"; then
    echo "FAILED: --sort stable:height reused the stable:area cache entry" >&2
    exit 1
fi

echo "Test 3: compact prewarm must not cache layouts for profiles with other settings"
warm_dir="$(fresh_copy prewarm_warm)"
cold_dir="$(fresh_copy prewarm_cold)"
layout_body "$warm_dir" --profiles-config "$profiles_cfg" --profile space --rotate > /dev/null
layout_body "$warm_dir" --profiles-config "$profiles_cfg" --profile desktop --rotate > "$tmp_dir/prewarm_warm.txt"
layout_body "$cold_dir" --profiles-config "$profiles_cfg" --profile desktop --rotate > "$tmp_dir/prewarm_cold.txt"
if ! diff -u "$tmp_dir/prewarm_cold.txt" "$tmp_dir/prewarm_warm.txt"; then
    echo "FAILED: --profile desktop returned a layout prewarmed by --profile space" >&2
    exit 1
fi

echo "Test 4: --rotate-direction after a cached run with the other direction"
dir="$(fresh_copy rotation)"
layout_body "$dir" --rotate > /dev/null
if ! layout_body "$dir" --rotate --rotate-direction ccw | grep -q '^rotation ccw$'; then
    echo "FAILED: --rotate-direction ccw returned a cached clockwise layout" >&2
    exit 1
fi

echo "Test 5: --debug output is never returned for a plain run (or vice versa)"
dir="$(fresh_copy debug)"
"$spratlayout_bin" "$dir" --debug > /dev/null
if "$spratlayout_bin" "$dir" | grep -q '^#'; then
    echo "FAILED: a plain run returned the cached --debug output" >&2
    exit 1
fi
if ! "$spratlayout_bin" "$dir" --debug | grep -q '^# Sprat Layout Debug Info'; then
    echo "FAILED: --debug returned a cached layout without the debug header" >&2
    exit 1
fi

echo "Test 6: layouts prewarmed for another profile keep their aliases"
warm_dir="$(fresh_copy alias_warm)"
cold_dir="$(fresh_copy alias_cold)"
for d in "$tmp_dir/alias_warm" "$tmp_dir/alias_cold"; do
    first="$(find "$d" -name '*.png' | sort | head -n 1)"
    cp -p "$first" "$d/zz_duplicate.png"
done
same_limits=(--profiles-config "$profiles_cfg" --max-width 2048 --max-height 2048 --deduplicate exact)
layout_body "$warm_dir" --profile desktop "${same_limits[@]}" > /dev/null
layout_body "$warm_dir" --profile mobile "${same_limits[@]}" > "$tmp_dir/alias_warm.txt"
layout_body "$cold_dir" --profile mobile "${same_limits[@]}" > "$tmp_dir/alias_cold.txt"
if ! grep -q '^alias ' "$tmp_dir/alias_cold.txt"; then
    echo "FAILED: test setup has no duplicate sprite" >&2
    exit 1
fi
if ! diff -u "$tmp_dir/alias_cold.txt" "$tmp_dir/alias_warm.txt"; then
    echo "FAILED: --profile mobile returned a prewarmed layout without its aliases" >&2
    exit 1
fi

echo "cache_keys_test.sh: ok"
