#!/usr/bin/env bash
# spratpack must reject unknown options and options missing their value instead of
# silently ignoring them.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: spratpack_args_test.sh <spratpack-bin>" >&2
    exit 1
fi

pack_bin="$1"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF
if base64 --version 2>&1 | grep -q "GNU"; then
    base64 -d "$tmp_dir/pixel.b64" > "$tmp_dir/pixel.png"
else
    base64 -D -i "$tmp_dir/pixel.b64" -o "$tmp_dir/pixel.png"
fi

layout="$tmp_dir/layout.txt"
cat > "$layout" <<EOF
atlas 1,1
sprite "$(fix_path "$tmp_dir/pixel.png")" 0,0 1,1
EOF

echo "Test 1: valid invocation succeeds"
"$pack_bin" --threads 1 < "$layout" > "$tmp_dir/ok.png"

echo "Test 2: unknown option fails"
if "$pack_bin" --no-such-option < "$layout" > /dev/null 2> "$tmp_dir/err.txt"; then
    echo "FAILED: spratpack accepted an unknown option" >&2
    exit 1
fi
grep -q -- "--no-such-option" "$tmp_dir/err.txt"

echo "Test 3: option without value fails"
if "$pack_bin" --extrude < "$layout" > /dev/null 2> "$tmp_dir/err.txt"; then
    echo "FAILED: spratpack accepted --extrude without a value" >&2
    exit 1
fi
grep -q -- "--extrude" "$tmp_dir/err.txt"

echo "Test 4: --zopfli with a non-PNG format fails"
if "$pack_bin" --zopfli --format webp < "$layout" > /dev/null 2>&1; then
    echo "FAILED: spratpack accepted --zopfli with --format webp" >&2
    exit 1
fi

echo "spratpack_args_test.sh: ok"
