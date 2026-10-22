#!/usr/bin/env bash
# spratlayout extracts archive inputs to a temporary folder. Each run must see only its
# own archive's files, and the folder must be removed however the run ends.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratlayout_archive_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
props_dir="$script_dir/assets/Streets of Fight files/Stage Layers/props"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# Private temp folder, so leftover extraction folders can be detected.
mkdir -p "$tmp_dir/tmp"
TMPDIR="$(fix_path "$tmp_dir/tmp")"
TMP="$TMPDIR"
TEMP="$TMPDIR"
export TMPDIR TMP TEMP

mkdir -p "$tmp_dir/a" "$tmp_dir/b"
cp "$props_dir/car.png" "$tmp_dir/a/"
cp "$props_dir/barrel.png" "$tmp_dir/b/"
(cd "$tmp_dir/a" && tar -cf ../a.tar car.png)
(cd "$tmp_dir/b" && tar -cf ../b.tar barrel.png)
a_tar="$(fix_path "$tmp_dir/a.tar")"
b_tar="$(fix_path "$tmp_dir/b.tar")"

leftovers() {
    find "$tmp_dir/tmp" -maxdepth 1 -name 'spratlayout_extract_*' | wc -l | tr -d ' '
}

echo "Test 1: a repeated archive run leaves no extraction folder behind"
"$spratlayout_bin" "$a_tar" > /dev/null
"$spratlayout_bin" "$a_tar" > /dev/null
if [ "$(leftovers)" != "0" ]; then
    echo "FAILED: extraction folder left in the temp directory" >&2
    exit 1
fi

echo "Test 2: the next archive contains only its own sprites"
"$spratlayout_bin" "$b_tar" | tr -d '\r' > "$tmp_dir/b.txt"
if grep -q 'car.png' "$tmp_dir/b.txt" || ! grep -q '"barrel.png"' "$tmp_dir/b.txt"; then
    echo "FAILED: layout of b.tar is wrong:" >&2
    cat "$tmp_dir/b.txt" >&2
    exit 1
fi

echo "Test 3: failed runs remove their extraction folder"
"$spratlayout_bin" "$a_tar" --max-width 10 > /dev/null 2>&1 && {
    echo "FAILED: expected the layout to fail" >&2
    exit 1
}
tar -cf - -C "$tmp_dir/a" car.png | "$spratlayout_bin" - --max-width 10 > /dev/null 2>&1 || true
if [ "$(leftovers)" != "0" ]; then
    echo "FAILED: extraction folder left after a failed run" >&2
    exit 1
fi

echo "spratlayout_archive_test.sh: ok"
