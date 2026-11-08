# spratpack: atlas images

`spratpack` reads a layout from stdin, loads the images and writes the atlas:
- Single atlas: PNG to stdout.
- Multipack layout: TAR stream (containing atlas PNG files) to stdout.

```sh
./build/spratpack < layout.txt > spritesheet.png
```

Optional frame divider overlay and extrusion:

- `--extrude N` (repeat edge pixels N times, overrides layout)
- `--frame-lines` (draw sprite rectangle outlines)
- `--line-width N` (default: `1`)
- `--line-color R,G,B[,A]` (default: `255,0,0,255`)
- `--threads N` (parallel sprite decode/blit when sprite rectangles do not overlap)

Example:

```sh
./build/spratpack --frame-lines --line-width 2 --line-color 0,255,0 < layout.txt > spritesheet.png
```

## Advanced options

### Zopfli Compression
Produce smaller PNGs using the Zopfli algorithm. It is significantly slower but provides better compression than standard deflate. Only valid for PNG output; `spratpack` exits with an error if it was built without zopflipng support.
```sh
./build/spratpack --zopfli < layout.txt > optimized.png
```

### Protection & Obfuscation
Protect your assets with basic XOR-based obfuscation.
```sh
./build/spratpack --protect < layout.txt > protected.png
```
`spratunpack` and other tools in the suite automatically handle de-obfuscation when they detect the "SPRAT!" signature.

### Artifact Reduction (Dilation)
Prevent dark halos around sprites in GPU bilinear filtering by bleeding opaque pixel colors into adjacent transparent pixels.
```sh
# Single pass dilation
./build/spratpack --dilate 1 < layout.txt > dilated.png

# Multiple passes for stronger effect
./build/spratpack --dilate 2 < layout.txt > dilated.png
```
The `--dilate N` flag performs N passes of color dilation around each sprite, filling transparent pixels up to N pixels away from visible ones with their RGB values while keeping alpha at 0. This is essential when using `--extrude` or padding, as GPU sampling can pull transparent pixels at edges. Dilation and extrusion never write inside another sprite's rectangle: with less padding than `2 × extrude` between two sprites, `spratpack --extrude` is cut short between them instead of overwriting them.

### Hardware Texture Compression
Compress atlases to GPU-native DXT formats (requires libsquish library).
```sh
# Compress to DXT1 (RGB, best compression)
./build/spratpack --gpu-compress dxt1 < layout.txt > atlas.dds

# Compress to DXT5 (RGBA with alpha channel)
./build/spratpack --gpu-compress dxt5 < layout.txt > atlas.dds
```
DXT/BC compression provides 4:1 or 6:1 compression ratios and loads directly into GPU memory without decompression. Atlases are padded with transparent pixels to a multiple of 4 (Direct3D 10+ require it); sprite coordinates are unchanged, since the padding is at the right and bottom. Fully transparent pixels take the colour of the visible pixels in their 4x4 block before compression, so they don't darken sprite edges.

`--gpu-quality fast` (default) uses libsquish's range fit; `--gpu-quality best` uses cluster fit, about 1–2 dB better and roughly 25x slower (a 2048x2048 atlas: about 0.5 s versus 12 s on 12 threads). Compression runs on `--threads` threads and gives the same file with any count. Atlases larger than about 23170x23170 are refused (libsquish's size limit; Direct3D itself supports up to 16384x16384).

libsquish is used when installed (`apt install libsquish-dev`, `brew install squish`, vcpkg `squish`); otherwise CMake downloads and builds the pinned 1.15 release into `_deps`. Pass `-DSPRAT_DOWNLOAD_SQUISH=OFF` to build without it, in which case `--gpu-compress` reports that it is not compiled in.

### Output Scale
Scale the whole output atlas (dimensions and sprite positions) by a factor. `--scale-filter` selects the resampling filter.
```sh
./build/spratpack --output-scale 0.5 < layout.txt > atlas_half.png
```

### Variant Atlases
Pack variants of the same sprites (palette swaps, skins, ...) with one layout, so every sprite sits at the same place in each atlas. `--path-suffix` loads `hero_red.png` instead of `hero.png`, and so on; the variant images should have the same sizes as the originals, because positions and trims come from the layout.
```sh
./build/spratlayout ./frames > layout.txt
./build/spratpack -a atlas_%d.png < layout.txt
./build/spratpack --path-suffix _red -a atlas_red_%d.png < layout.txt
```

Layouts can also ask `spratpack` for [nine-slice metadata and per-sprite dithering](layout-format.md).

## Complete optimization workflow

Here's an example combining multiple features for maximum optimization:

```sh
# 1. Generate layout with deduplication and trimming
./build/spratlayout ./frames \
  --deduplicate exact \
  --trim-transparent \
  --padding 2 \
  --extrude 1 \
  --multipack \
  > layout.txt

# 2. Pack with artifact reduction (dilation) and zopfli compression
./build/spratpack \
  --dilate 1 \
  --zopfli \
  --frame-lines \
  < layout.txt > spritesheet.png

# 3. Generate metadata for game engine
./build/spratconvert \
  --transform json \
  --auto-animations \
  < layout.txt > layout.json
```

This pipeline:
- Removes duplicate sprites (saves atlas space)
- Trims transparent borders (reduces pixel bloat)
- Adds safe padding/extrusion (prevents sampling artifacts)
- Enables dilation (bleeds colors to prevent halos)
- Compresses with Zopfli (smallest possible file)
- Generates game-ready metadata with animations

For GPU-native compression (when libsquish is available):
```sh
./build/spratlayout ./frames --deduplicate exact --trim-transparent --padding 2 | \
./build/spratpack --dilate 1 --gpu-compress dxt5 > atlas.dds
```

[Back to the README](../README.md)
