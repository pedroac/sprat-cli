#!/usr/bin/env bash
# Regenerate tests/baselines/assets/*.txt from the current build.
# Usage: scripts/update_asset_baselines.sh [BUILD_DIR] [CASE...]
#   BUILD_DIR defaults to ./build. Pass case names to update only those.
# Review the diff (git diff tests/baselines) before committing.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_dir/build}"
shift $(( $# > 0 ? 1 : 0 ))

find_bin() {
    local name="$1" candidate
    for candidate in "$build_dir/$name" "$build_dir/$name.exe" \
                     "$build_dir/Release/$name.exe" "$build_dir/Debug/$name.exe"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "Error: $name not found in $build_dir (build first)" >&2
    exit 1
}

python_bin="python3"
command -v python3 >/dev/null 2>&1 || python_bin="python"

"$python_bin" "$repo_dir/tests/assets_regression.py" \
    --spratlayout "$(find_bin spratlayout)" \
    --spratpack "$(find_bin spratpack)" \
    --spratframes "$(find_bin spratframes)" \
    --update "$@"
