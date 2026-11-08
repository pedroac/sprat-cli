# spratpack: Complete Reference

`spratpack` reads a layout and generates the sprite sheet image(s).

## Output

- Single atlas → PNG to stdout
- Multipack layout → TAR stream (containing PNG files) to stdout

## All options

### Output specification

- `-a FORMAT` — Output filename pattern (e.g., `-a atlas_%d.png`, `-a sheet.png`)
  - For single atlases: `-a output.png` writes to that file
  - For multipack: `-a atlas_%d.png` generates `atlas_0.png`, `atlas_1.png`, etc.
- `--atlas-index N` — Extract only atlas index N (useful for multipack debugging)

### Image composition

- `--frame-lines` — Draw sprite boundary rectangles
- `--line-width N` — Width of boundary lines (default: 1)
- `--line-color R,G,B[,A]` — Line color in RGBA (default: 255,0,0,255 for red)
- `--extrude N` — Repeat edge pixels N times (overrides layout); prevents sampling artifacts at edges
- `--dilate N` — Bleed opaque pixels into transparent margins (prevents dark halos with GPU bilinear filtering)
- `--output-scale F` — Scale entire atlas by factor (e.g., 0.5 for half size); affects dimensions and sprite positions

### Compression & format

- `--zopfli` — Use Zopfli algorithm for smaller PNG (significantly slower; better than standard deflate)
- `--gpu-compress dxt1|dxt5` — Compress to GPU-native DXT format
  - `dxt1` — RGB, best compression (4:1 ratio)
  - `dxt5` — RGBA with alpha (6:1 ratio)
  - Requires libsquish; outputs `.dds` files
- `--gpu-quality fast|best` — Compression quality for `--gpu-compress` (default: `fast`)
  - `fast` — range fit algorithm
  - `best` — cluster fit, ~25x slower, 1–2 dB better quality
- `--protect` — Basic XOR-based obfuscation (sets "SPRAT!" signature; `spratunpack` auto-detects and deobfuscates)

### Variants & scaling

- `--path-suffix SUFFIX` — Load variant images with suffix (e.g., `--path-suffix _red` loads `hero_red.png` instead of `hero.png`)
- `--scale-filter lanczos|linear|cubic` — Resampling filter for `--output-scale` (default: `lanczos`)

### Performance

- `--threads N` — Parallel sprite decode and blit (when sprites don't overlap)

## Advanced: Dilation

Artifact reduction for GPU bilinear filtering.

```sh
# Single pass
./build/spratpack --dilate 1 < layout.txt > atlas.png

# Multiple passes for stronger effect
./build/spratpack --dilate 2 < layout.txt > atlas.png
```

Dilation performs N passes of color bleeding:
- Fills transparent pixels up to N pixels away from visible ones
- Uses RGB values of nearby opaque pixels
- Keeps alpha at 0 in filled pixels
- Never overwrites adjacent sprite rectangles

Essential when using `--extrude` or padding. With padding < `2 × extrude` between sprites, extrusion is cut short to avoid overwriting neighbors.

## Advanced: GPU compression (DXT/BC)

Hardware texture compression for direct GPU loading.

```sh
# Compress to DXT1 (RGB)
./build/spratpack --gpu-compress dxt1 < layout.txt > atlas.dds

# Compress to DXT5 (RGBA)
./build/spratpack --gpu-compress dxt5 < layout.txt > atlas.dds
```

Details:
- 4:1 (DXT1) or 6:1 (DXT5) compression ratios
- Loads directly into GPU memory without decompression
- Atlases padded to multiple of 4 (Direct3D 10+ requirement)
- Sprite coordinates unchanged (padding at right/bottom only)
- Fully transparent pixels use visible pixels' color in their 4×4 block (prevents edge darkening)
- Size limits: max ~23170×23170 (libsquish limit); Direct3D: 16384×16384

Requires libsquish:
- Install: `apt install libsquish-dev`, `brew install squish`, or vcpkg `squish`
- Fallback: CMake downloads pinned 1.15 release into `_deps`
- Disable with: `-DSPRAT_DOWNLOAD_SQUISH=OFF`

Quality settings:
- `fast` (default) — range fit, ~0.5s for 2048×2048 on 12 threads
- `best` — cluster fit, ~12s for 2048×2048 on 12 threads (same file regardless of thread count)

## Advanced: Zopfli compression

Slower PNG compression with better ratios than standard deflate.

```sh
./build/spratpack --zopfli < layout.txt > optimized.png
```

Significantly slower but produces smaller files. Only valid for PNG output.

## Advanced: Protection (obfuscation)

Basic XOR-based asset obfuscation.

```sh
./build/spratpack --protect < layout.txt > protected.png
```

Details:
- Sets "SPRAT!" magic signature
- `spratunpack` and other tools auto-detect and deobfuscate
- Basic obfuscation only (not cryptographic security)

## Variant atlases (palette swaps)

Pack variants of the same sprites (skins, color schemes) at identical positions.

```sh
# Original sprites
./build/spratlayout ./frames > layout.txt

# Generate base atlas
./build/spratpack -a atlas.png < layout.txt

# Generate variant atlases
./build/spratpack --path-suffix _red -a atlas_red.png < layout.txt
./build/spratpack --path-suffix _blue -a atlas_blue.png < layout.txt
```

Requirements:
- Variant images must have identical dimensions to originals
- Positions and trim come from single layout
- Useful for character skins, color variants, cosmetics

## Nine-slice & dithering

Layouts can specify per-sprite metadata:
- [Nine-slice (9-patch) data](../advanced/layout-format.md#nine-slice)
- [Per-sprite dithering](../advanced/layout-format.md#dithering)

## Complete optimization workflow

Example combining multiple optimizations:

```sh
# 1. Generate layout with deduplication and trimming
./build/spratlayout ./frames \
  --deduplicate exact \
  --trim-transparent \
  --padding 2 \
  --extrude 1 \
  --multipack \
  > layout.txt

# 2. Pack with artifact reduction and compression
./build/spratpack \
  --dilate 1 \
  --zopfli \
  --frame-lines \
  < layout.txt > spritesheet.png

# 3. Generate game-ready metadata
./build/spratconvert \
  --transform json \
  --auto-animations \
  < layout.txt > layout.json
```

Result:
- Duplicate sprites removed (saves space)
- Transparent borders trimmed (reduces bloat)
- Safe padding and extrusion (prevents sampling artifacts)
- Color dilation (prevents halos in GPU filtering)
- Zopfli compression (smallest file)
- Game-ready metadata with animations

For GPU-native compression:
```sh
./build/spratlayout ./frames \
  --deduplicate exact \
  --trim-transparent \
  --padding 2 | \
./build/spratpack \
  --dilate 1 \
  --gpu-compress dxt5 \
  > atlas.dds
```

[See spratpack guide →](../guides/spratpack.md) | [Back to README](../../README.md)
