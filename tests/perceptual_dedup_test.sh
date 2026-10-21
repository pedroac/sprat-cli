#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: perceptual_dedup_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"

if ! command -v python3 >/dev/null 2>&1 || ! python3 -c "import PIL" >/dev/null 2>&1; then
    echo "perceptual_dedup_test.sh: skipped (python3 with PIL not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

# Isolate test from user configuration
mkdir -p "$tmp_dir/.config/sprat"
printf '[profile fast]\nmode=fast\n' > "$tmp_dir/.config/sprat/spratprofiles.cfg"
export HOME="$tmp_dir"
if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    export USERPROFILE="$(cygpath -w "$tmp_dir")"
    frames_dir="$(cygpath -m "$tmp_dir/frames")"
else
    frames_dir="$tmp_dir/frames"
fi
mkdir -p "$tmp_dir/frames"

# 36x32 images made of 4x4 blocks, so each block maps onto one dHash sample.
#   a:     random blocks, with blocks 0 and 1 set to 100 and 102
#   b:     a with blocks 0 and 1 swapped: 1 dHash bit and 2 levels per pixel apart
#   c:     a with one block inverted: hash is close, pixels are not (must never merge)
#   flat1, flat2: flat colours 1 level apart (their dHash is 0, which is valid)
#   flat3: a different flat colour (must never merge)
python3 - "$frames_dir" <<'PY'
import random, sys
from PIL import Image

def make(values, path):
    im = Image.new("RGBA", (36, 32))
    for by in range(8):
        for bx in range(9):
            v = values[by * 9 + bx]
            for y in range(4):
                for x in range(4):
                    im.putpixel((bx * 4 + x, by * 4 + y), (v, v, v, 255))
    im.save(path)

rng = random.Random(1)
a = [rng.randrange(256) for _ in range(72)]
a[0], a[1] = 100, 102
b = list(a)
b[0], b[1] = 102, 100
c = list(a)
c[40] = 255 - c[40]
out = sys.argv[1]
make(a, out + "/a.png")
make(b, out + "/b.png")
make(c, out + "/c.png")
Image.new("RGBA", (16, 16), (200, 40, 40, 255)).save(out + "/flat1.png")
Image.new("RGBA", (16, 16), (201, 40, 40, 255)).save(out + "/flat2.png")
Image.new("RGBA", (16, 16), (40, 40, 200, 255)).save(out + "/flat3.png")
PY

aliases() {
    "$spratlayout_bin" "$frames_dir" --deduplicate "$@" | grep '^alias' | sort || true
}

expect_aliases() {
    local expected="$1"; shift
    local actual
    actual="$(aliases "$@" | tr -d '"' | awk '{ print ($2 < $3) ? $2 "=" $3 : $3 "=" $2 }' | sort | tr '\n' ' ')"
    if [ "$actual" != "$expected" ]; then
        echo "--deduplicate $*: expected [$expected], got [$actual]" >&2
        exit 1
    fi
}

# exact never merges near-identical sprites
expect_aliases "" exact
# threshold 0 requires identical hashes: a/b (1 bit apart) stay separate, flats merge
expect_aliases "flat1.png=flat2.png " perceptual --dedup-threshold 0
# defaults merge a/b and the near-identical flats
expect_aliases "a.png=b.png flat1.png=flat2.png " perceptual
# even the loosest hash threshold never merges sprites whose pixels differ (c, flat3)
expect_aliases "a.png=b.png flat1.png=flat2.png " perceptual --dedup-threshold 64
# pixel tolerance: a/b differ by 2 levels, flat1/flat2 by 1
expect_aliases "flat1.png=flat2.png " perceptual --dedup-tolerance 1
expect_aliases "" perceptual --dedup-tolerance 0
# changing the options again must not be served from a stale cache
expect_aliases "a.png=b.png flat1.png=flat2.png " perceptual

# invalid values are rejected
expect_rejected() {
    local flag="$1" value="$2" message="$3"
    if "$spratlayout_bin" "$frames_dir" --deduplicate perceptual "$flag" "$value" \
            > /dev/null 2> "$tmp_dir/err.txt"; then
        echo "Expected failure for $flag $value" >&2
        exit 1
    fi
    grep -q "$message" "$tmp_dir/err.txt"
}
for bad in -1 65 abc; do
    expect_rejected --dedup-threshold "$bad" 'Invalid dedup threshold'
done
for bad in -1 256 abc; do
    expect_rejected --dedup-tolerance "$bad" 'Invalid dedup tolerance'
done

echo "perceptual_dedup_test.sh: ok"
