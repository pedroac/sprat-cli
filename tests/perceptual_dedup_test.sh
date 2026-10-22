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

# ── Aliases keep their own trim; nine-slice insets block merging ──────────────
# Same trimmed pixels at a different place in the canvas is still a duplicate, but the
# alias must record its own trim or engines restoring the canvas would misplace it.
mkdir -p "$tmp_dir/offsets"
offsets_dir="$frames_dir/../offsets"
python3 - "$offsets_dir" <<'PY'
import sys
from PIL import Image

def frame(path, top):
    im = Image.new("RGBA", (16, 24))
    for y in range(8):
        for x in range(8):
            im.putpixel((4 + x, top + y), (200, 40, 40, 255))
    im.save(path)

def nine(path, guide):
    # 12x12 interior inside the 1px nine-patch border; guides mark the stretch region.
    im = Image.new("RGBA", (14, 14), (40, 200, 40, 255))
    for i in range(14):
        im.putpixel((i, 0), (0, 0, 0, 0))
        im.putpixel((0, i), (0, 0, 0, 0))
        im.putpixel((i, 13), (0, 0, 0, 0))
        im.putpixel((13, i), (0, 0, 0, 0))
    for i in range(guide, 13 - guide):
        im.putpixel((i, 0), (0, 0, 0, 255))
        im.putpixel((0, i), (0, 0, 0, 255))
    im.save(path)

out = sys.argv[1]
frame(out + "/idle_00.png", 10)
frame(out + "/idle_01.png", 4)
frame(out + "/idle_02.png", 10)
nine(out + "/btn_a.9.png", 3)
nine(out + "/btn_b.9.png", 3)
nine(out + "/btn_c.9.png", 5)
PY
"$spratlayout_bin" "$offsets_dir" --trim-transparent --deduplicate exact > "$tmp_dir/offsets.txt"
python3 - "$tmp_dir/offsets.txt" <<'PY'
import sys
sprites, aliases = {}, {}
for line in open(sys.argv[1]):
    parts = line.split()
    if parts and parts[0] == "sprite":
        sprites[parts[1].strip('"')] = parts[2:]
    elif parts and parts[0] == "alias":
        aliases[parts[1].strip('"')] = (parts[2].strip('"'), parts[3:])
idle = sorted(n for n in sprites if n.startswith("idle"))
assert len(idle) == 1, (sprites, aliases)
canonical = idle[0]
top = int(sprites[canonical][2].split(",")[1])
for name in ("idle_00.png", "idle_01.png", "idle_02.png"):
    if name == canonical:
        continue
    target, trim = aliases[name]
    assert target == canonical, (name, target)
    own_top = 4 if name == "idle_01.png" else 10
    if own_top == top:
        assert trim == [], (name, trim)  # same offsets: short alias line
    else:
        assert trim == ["4,%d" % own_top, "4,%d" % (16 - own_top)], (name, trim)
# btn_a and btn_b share pixels and guides; btn_c has the same pixels but other insets
buttons = sorted(n for n in sprites if n.startswith("btn"))
assert len(buttons) == 2 and "btn_c.9.png" in buttons, (sprites, aliases)
assert {n for n in aliases if n.startswith("btn")} <= {"btn_a.9.png", "btn_b.9.png"}, aliases
PY
"$spratlayout_bin" "$offsets_dir" --trim-transparent --deduplicate perceptual > "$tmp_dir/offsets_p.txt"
if grep -q '"btn_c.9.png"' <(grep '^alias' "$tmp_dir/offsets_p.txt"); then
    echo "perceptual dedup merged sprites with different nine-slice insets" >&2
    exit 1
fi

echo "perceptual_dedup_test.sh: ok"
