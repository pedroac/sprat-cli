#!/usr/bin/env bash
# Regression: a folder input must give the same layout on every filesystem. Directory
# listing order is filesystem-specific (ext4 hash order, NTFS, APFS, ...), so spratlayout
# sorts folder images by natural name: the layout of a folder must equal the layout of a
# --stdin-list of the same files in natural order ("walk2" before "walk10").
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: folder_order_test.sh <spratlayout-bin>" >&2
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

mkdir -p "$tmp_dir/cache"
TMPDIR="$(fix_path "$tmp_dir/cache")"
TMP="$TMPDIR"
TEMP="$TMPDIR"
export TMPDIR TMP TEMP
export HOME="$tmp_dir"
export XDG_CONFIG_HOME="$tmp_dir/.config"

# Sprites of different sizes, created in reverse name order, with names whose plain
# string order differs from their natural order.
frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"
# The physical path: --stdin-list takes its root from the working directory, which the OS
# reports resolved (macOS: /var/folders/... is /private/var/folders/...).
frames_dir="$(cd "$frames_dir" && pwd -P)"
names=(walk1 walk2 walk3 walk10 walk11 walk20 idle)
sources=("$croc_dir"/*/*.png)
for ((i = ${#names[@]} - 1; i >= 0; i--)); do
    cp "${sources[$i]}" "$frames_dir/${names[$i]}.png"
done
natural_order="idle.png
walk1.png
walk2.png
walk3.png
walk10.png
walk11.png
walk20.png"

for mode in fast compact; do
    "$spratlayout_bin" "$(fix_path "$frames_dir")" --mode "$mode" > "$tmp_dir/folder_$mode.txt"
    (cd "$frames_dir" && printf '%s\n' "$natural_order" \
        | "$spratlayout_bin" --stdin-list --mode "$mode") > "$tmp_dir/list_$mode.txt"
    if ! diff -u "$tmp_dir/list_$mode.txt" "$tmp_dir/folder_$mode.txt" >&2; then
        echo "FAIL: --mode $mode folder layout differs from the naturally ordered list" >&2
        exit 1
    fi
done

echo "folder_order test passed"
