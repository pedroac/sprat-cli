#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: assets_regression_test.sh <spratlayout-bin> <spratpack-bin> <spratframes-bin>" >&2
    exit 1
fi

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "assets_regression_test.sh: skipped (python3 with PIL not available)"
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$script_dir/assets_regression.py"
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    script="$(cygpath -m "$script")"
fi

exec "$python_bin" "$script" \
    --spratlayout "$1" --spratpack "$2" --spratframes "$3"
