#!/usr/bin/env bash
# spratpack's optional encoders must produce valid files when compiled in: DXT1/DXT5 DDS
# (libsquish), WebP, AVIF and Zopfli-optimised PNG. Encoders a build lacks are skipped, but
# one that is reported as available must work.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: codec_output_test.sh <spratpack-bin>" >&2
    exit 1
fi

pack_bin="$1"

python_bin=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import PIL" >/dev/null 2>&1; then
        python_bin="$candidate"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "codec_output_test.sh: skipped (python with Pillow not available)"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    fix_path() { cygpath -m "$1"; }
else
    fix_path() { echo "$1"; }
fi

# 61x30 image: gradient, hard edges, a semi-transparent ramp and a transparent area. 61x30 is
# not a multiple of 4, so DDS output has to pad it.
"$python_bin" - "$(fix_path "$tmp_dir/tex.png")" <<'EOF'
import sys
from PIL import Image
im = Image.new("RGBA", (61, 30), (0, 0, 0, 0))
for y in range(30):
    for x in range(61):
        if x < 20:
            im.putpixel((x, y), (x * 12, y * 8, 128, 255))
        elif x < 40:
            im.putpixel((x, y), (255, 255, 255, 255) if (x // 3 + y // 3) % 2 else (200, 20, 20, 255))
        elif x < 52:
            im.putpixel((x, y), (30, 160, 220, int(255 * y / 29)))
im.save(sys.argv[1])
EOF
printf 'atlas 61,30\nsprite "%s" 0,0 61,30\n' "$(fix_path "$tmp_dir/tex.png")" > "$tmp_dir/layout.txt"
"$pack_bin" < "$tmp_dir/layout.txt" > "$tmp_dir/ref.png"

available() {
    # An encoder that is not compiled in fails with "not compiled in".
    if "$pack_bin" "$@" < "$tmp_dir/layout.txt" > "$tmp_dir/probe.out" 2> "$tmp_dir/probe.err"; then
        return 0
    fi
    if grep -q 'not compiled in' "$tmp_dir/probe.err"; then
        return 1
    fi
    echo "FAILED: spratpack $* failed:" >&2
    cat "$tmp_dir/probe.err" >&2
    exit 1
}

if available --gpu-compress dxt5; then
    echo "Test: DDS (libsquish)"
    for format in dxt1 DXT5 Dxt1; do
        for quality in fast best; do
            "$pack_bin" --gpu-compress "$format" --gpu-quality "$quality" < "$tmp_dir/layout.txt" > "$tmp_dir/out.dds"
            "$python_bin" - "$(fix_path "$tmp_dir/out.dds")" "$(fix_path "$tmp_dir/ref.png")" "$format" "$quality" <<'EOF'
import math, struct, sys
from PIL import Image
path, ref_path, fmt, quality = sys.argv[1], sys.argv[2], sys.argv[3].lower(), sys.argv[4]
raw = open(path, "rb").read()
magic, size, flags, h, w, linear, depth, mips = struct.unpack_from("<4s7I", raw, 0)
pf_size, pf_flags, fourcc = struct.unpack_from("<II4s", raw, 76)
block_bytes = 8 if fmt == "dxt1" else 16
problems = []
if magic != b"DDS " or size != 124 or pf_size != 32 or pf_flags != 0x4:
    problems.append("bad header")
if fourcc != fmt.upper().encode():
    problems.append(f"fourcc {fourcc!r}")
if flags & 0x81007 != 0x81007:
    problems.append(f"flags {flags:#x}")
if (w, h) != (64, 32):
    problems.append(f"size {w}x{h}, expected the 61x30 atlas padded to 64x32")
if linear != (w // 4) * (h // 4) * block_bytes or len(raw) != 128 + linear:
    problems.append("data size")
ref = Image.open(ref_path).convert("RGBA")
dec = Image.open(path).convert("RGBA")
err = n = alpha_bad = 0
for y in range(dec.size[1]):
    for x in range(dec.size[0]):
        d = dec.getpixel((x, y))
        if x >= ref.size[0] or y >= ref.size[1]:
            alpha_bad += d[3] != 0  # padding stays transparent
            continue
        r = ref.getpixel((x, y))
        if fmt == "dxt1":
            alpha_bad += (r[3] >= 128) != (d[3] == 255)
        else:
            alpha_bad += abs(r[3] - d[3]) > 4
        if r[3] >= 128:
            err += sum((r[i] - d[i]) ** 2 for i in range(3))
            n += 3
psnr = 10 * math.log10(255 ** 2 / (err / n)) if err else 99
if alpha_bad:
    problems.append(f"{alpha_bad} pixels with wrong alpha")
if psnr < (33 if quality == "fast" else 35):
    problems.append(f"colour PSNR {psnr:.1f} dB")
if problems:
    sys.exit(f"FAILED: {fmt} {quality}: " + "; ".join(problems))
EOF
        done
    done
    "$pack_bin" --gpu-compress dxt5 --threads 1 < "$tmp_dir/layout.txt" > "$tmp_dir/t1.dds"
    "$pack_bin" --gpu-compress dxt5 --threads 8 < "$tmp_dir/layout.txt" > "$tmp_dir/t8.dds"
    cmp -s "$tmp_dir/t1.dds" "$tmp_dir/t8.dds" || { echo "FAILED: DDS output depends on --threads" >&2; exit 1; }
    # libsquish computes sizes and offsets in int: an atlas whose RGBA data exceeds INT_MAX
    # bytes (here a 65540x65536 layout, which once wrapped the DDS size to 256 KB and made the
    # encoder write gigabytes past it) is refused before any memory is allocated.
    for size in 65540,65536 65536,65536 23172,23172; do
        status=0
        printf 'atlas %s\n' "$size" | "$pack_bin" --gpu-compress dxt5 > /dev/null 2> "$tmp_dir/dds_big.err" || status=$?
        if [ "$status" -ne 1 ] || ! grep -q 'too large for --gpu-compress' "$tmp_dir/dds_big.err"; then
            cat "$tmp_dir/dds_big.err" >&2
            echo "FAILED: a ${size/,/x} atlas must be refused for DDS (exit $status)" >&2
            exit 1
        fi
    done
else
    echo "Test: DDS skipped (libsquish not compiled in)"
fi

if available --format webp; then
    echo "Test: WebP"
    "$pack_bin" --format webp < "$tmp_dir/layout.txt" > "$tmp_dir/out.webp"
    "$python_bin" - "$(fix_path "$tmp_dir/out.webp")" "$(fix_path "$tmp_dir/ref.png")" <<'EOF'
import sys
from PIL import Image
out, ref = Image.open(sys.argv[1]), Image.open(sys.argv[2]).convert("RGBA")
if out.format != "WEBP" or out.size != ref.size or out.convert("RGBA").tobytes() != ref.tobytes():
    sys.exit("FAILED: default WebP output is not a lossless copy of the atlas (including the colour of transparent pixels)")
EOF
else
    echo "Test: WebP skipped (not compiled in)"
fi

if available --format avif; then
    echo "Test: AVIF"
    "$pack_bin" --format avif < "$tmp_dir/layout.txt" > "$tmp_dir/out.avif"
    head -c 12 "$tmp_dir/out.avif" | tail -c 8 | grep -q 'ftypavif' || { echo "FAILED: AVIF output lacks the ftyp avif header" >&2; exit 1; }
    # Pillow decodes AVIF when built with it; the default output is lossless.
    "$python_bin" - "$(fix_path "$tmp_dir/out.avif")" "$(fix_path "$tmp_dir/ref.png")" <<'EOF'
import sys
from PIL import Image, features
if features.check("avif"):
    out, ref = Image.open(sys.argv[1]).convert("RGBA"), Image.open(sys.argv[2]).convert("RGBA")
    if out.size != ref.size or out.tobytes() != ref.tobytes():
        sys.exit("FAILED: default AVIF output is not a lossless copy of the atlas")
EOF
else
    echo "Test: AVIF skipped (not compiled in)"
fi

if available --zopfli; then
    echo "Test: Zopfli"
    "$pack_bin" --zopfli < "$tmp_dir/layout.txt" > "$tmp_dir/out_z.png"
    "$python_bin" - "$(fix_path "$tmp_dir/out_z.png")" "$(fix_path "$tmp_dir/ref.png")" <<'EOF'
import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert("RGBA"), Image.open(sys.argv[2]).convert("RGBA")
if a.tobytes() != b.tobytes():
    sys.exit("FAILED: Zopfli changed the pixels")
EOF
else
    echo "Test: Zopfli skipped (not compiled in)"
fi

echo "codec_output_test.sh: ok"
