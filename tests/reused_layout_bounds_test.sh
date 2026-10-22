#!/usr/bin/env bash
# Regression: positions reused from an earlier run (the layout seed or --incremental) must
# still follow the mode's rules: pot atlases keep power-of-two sides, and grid layouts are
# never pieced together from earlier positions.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: reused_layout_bounds_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
croc_dir="$script_dir/assets/Crocodile Mascot Running Game/transparent PNG"

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
# No profiles config: only command-line options apply.
export HOME="$tmp_dir"
export XDG_CONFIG_HOME="$tmp_dir/.config"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

atlas_size() {
    grep '^atlas ' "$1" | head -n 1 | cut -d' ' -f2
}

is_power_of_two() {
    local v="$1"
    [ "$v" -gt 0 ] && [ $((v & (v - 1))) -eq 0 ]
}

assert_pot_atlas() {
    local layout="$1" label="$2"
    local size w h
    size="$(atlas_size "$layout")"
    w="${size%,*}"
    h="${size#*,}"
    if ! is_power_of_two "$w" || ! is_power_of_two "$h"; then
        cat "$layout" >&2
        fail "$label: pot atlas is ${w}x${h}"
    fi
}

# The cache is keyed by folder, so each scenario gets its own folder.
new_folder() {
    local dir="$tmp_dir/$1"
    mkdir -p "$dir"
    echo "$dir"
}

# 1) A pot run whose output-cache key differs only in options the seed ignores (rotation
#    direction, deduplication) reuses the seed positions; the atlas must stay power-of-two.
seed_dir="$(new_folder pot_seed)"
cp "$croc_dir"/run/frame-1.png "$croc_dir"/run/frame-2.png "$croc_dir"/run/frame-3.png "$seed_dir/"
"$spratlayout_bin" "$(fix_path "$seed_dir")" --mode pot > "$tmp_dir/seed_first.txt"
assert_pot_atlas "$tmp_dir/seed_first.txt" "pot run"
"$spratlayout_bin" "$(fix_path "$seed_dir")" --mode pot --rotate-direction ccw > "$tmp_dir/seed_ccw.txt"
assert_pot_atlas "$tmp_dir/seed_ccw.txt" "pot run reusing the seed (--rotate-direction ccw)"
"$spratlayout_bin" "$(fix_path "$seed_dir")" --mode pot --deduplicate exact > "$tmp_dir/seed_dedup.txt"
assert_pot_atlas "$tmp_dir/seed_dedup.txt" "pot run reusing the seed (--deduplicate exact)"

# 2) --incremental in pot mode keeps power-of-two sides after a sprite is added, and its
#    result doesn't leak into a later plain pot run through the seed.
inc_dir="$(new_folder pot_incremental)"
cp "$croc_dir"/run/frame-1.png "$croc_dir"/run/frame-2.png "$croc_dir"/run/frame-3.png "$inc_dir/"
"$spratlayout_bin" "$(fix_path "$inc_dir")" --mode pot --incremental > "$tmp_dir/inc_first.txt"
assert_pot_atlas "$tmp_dir/inc_first.txt" "first pot --incremental run"
cp "$croc_dir"/idle/frame-1.png "$inc_dir/idle.png"
"$spratlayout_bin" "$(fix_path "$inc_dir")" --mode pot --incremental > "$tmp_dir/inc_second.txt"
assert_pot_atlas "$tmp_dir/inc_second.txt" "pot --incremental run after adding a sprite"
grep -q '^sprite "idle.png"' "$tmp_dir/inc_second.txt" || fail "added sprite missing from incremental layout"
"$spratlayout_bin" "$(fix_path "$inc_dir")" --mode pot > "$tmp_dir/inc_plain.txt"
assert_pot_atlas "$tmp_dir/inc_plain.txt" "plain pot run after pot --incremental"

# 3) Grid mode ignores --incremental: sprites of different sizes are still refused instead
#    of being added next to the earlier cells.
grid_dir="$(new_folder grid_incremental)"
cp "$croc_dir"/run/frame-1.png "$croc_dir"/run/frame-2.png "$grid_dir/"
"$spratlayout_bin" "$(fix_path "$grid_dir")" --mode grid --incremental \
    > "$tmp_dir/grid_first.txt" 2> "$tmp_dir/grid_first.err"
grep -q 'ignored in grid mode' "$tmp_dir/grid_first.err" || fail "grid --incremental did not warn"
[ "$(grep -c '^sprite ' "$tmp_dir/grid_first.txt")" -eq 2 ] || fail "grid layout of equal sprites failed"
cp "$croc_dir"/idle/frame-1.png "$grid_dir/idle.png"
if "$spratlayout_bin" "$(fix_path "$grid_dir")" --mode grid --incremental \
    > "$tmp_dir/grid_second.txt" 2> "$tmp_dir/grid_second.err"; then
    cat "$tmp_dir/grid_second.txt" >&2
    fail "grid --incremental accepted sprites of different sizes"
fi
grep -q 'same size' "$tmp_dir/grid_second.err" || fail "grid error does not mention the size mismatch"

echo "reused_layout_bounds test passed"
