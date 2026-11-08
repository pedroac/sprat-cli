# spratlayout: Complete Reference

`spratlayout` scans images and computes where each one goes in the atlas. It prints a layout to stdout.

## All options

### Input & Output

- `PATH` — Directory, tar archive (`.tar`, `.tar.gz`, `.tar.bz2`, `.tar.xz`), or stdin if no arguments
- `--stdin-list` — Read filenames from stdin (one per line)
- `-` — Read image data from stdin (binary)

### Archive inputs

`spratlayout` accepts tar archives as input. Automatically extracts to a temporary directory; temp files cleaned up after processing.

```sh
./build/spratlayout sprites.tar > layout.txt
./build/spratlayout sprites.tar.gz > layout.txt
./build/spratlayout sprites.tar.bz2 > layout.txt
./build/spratlayout sprites.tar.xz > layout.txt
```

### Packing algorithm

- `--mode compact|pot|fast|grid` — Packing algorithm choice (default: `fast`)
  - `compact` — Optimized bin packing, slower but better utilization
  - `pot` — Power-of-two dimensions only (512×512, 1024×512, etc.)
  - `fast` — Shelf algorithm, fast but less efficient
  - `grid` — Uniform cells, left-to-right and top-to-bottom; all sprites must have the same size
- `--optimize gpu|space` — Optimize for GPU-friendly dimensions or minimum area (default: `gpu`)
- `--max-width N` — Maximum atlas width (no default limit)
- `--max-height N` — Maximum atlas height (no default limit)
- `--no-max-width` — Disable max width limit from profile
- `--no-max-height` — Disable max height limit from profile
- `--preset fast|quality|small|pot` — Shorthand that sets `mode` and `optimize`

### Spacing & trimming

- `--padding N` — Pixels between sprites (default: 0)
- `--extrude N` — Repeat edge pixels N times to prevent sampling artifacts; padding is raised to at least `extrude * 2`
- `--trim-transparent` — Remove empty borders from each sprite

### Sprite transformation

- `--rotate` — Allow 90-degree rotation during packing
- `--rotate-direction cw|ccw` — Direction rotated sprites are oriented (default: `cw`)
  - `cw` — Clockwise (TexturePacker, Phaser, Godot, Unity, Aseprite, plist)
  - `ccw` — Counter-clockwise (libGDX)
- `--scale F` — Pre-scale all images (0 < F ≤ 1); animation frames of one size stay one size at any scale
- `--source-resolution WxH` — Original design resolution (for calculating target scale)
- `--target-resolution WxH` — Target resolution; must be given together with `--source-resolution`
- `--resolution-reference largest|smallest` — Which axis ratio drives the scale (default: `largest`)

### Deduplication

- `--deduplicate none|exact|perceptual` — Detect and merge duplicate sprites (default: `none`)
  - `exact` — Pixel-identical sprites only (content hash)
  - `perceptual` — Near-identical sprites (dHash + per-pixel tolerance)
- `--dedup-threshold N` — Max perceptual-hash distance for candidate pairing (0–64, default: `5`)
- `--dedup-tolerance N` — Max per-channel pixel difference for merge (0–255, default: `8`)

### Multiple atlases

- `--multipack` — Allow splitting across multiple atlases when needed

### Sorting & ordering

- `--sort name|none|stable[:metric]` — Sort sprites in layout (default: `none`)
  - `name` — Natural filename order, enforced in the output
  - `none` — Keep the input order and let the packer reorder for efficiency
  - `stable[:metric]` — Deterministic order by size, then path; metric is `area` (default), `maxside`, `height`, `width` or `perimeter`
- Directory inputs always read in natural filename order for reproducibility

### Incremental packing

- `--incremental` — Reuse positions from the previous layout and pack only new/changed sprites. Single atlas only; `pot` keeps power-of-two sides; `grid` ignores it.

### Performance

- `--threads N` — Parallelize packing search
- `--debug` — Enable detailed error reporting and visualization

### Profiles

- `--profile NAME` — Load named profile from config file
- `--profiles-config PATH` — Override profile config location

Profile search paths:
1. `--profiles-config PATH` (CLI override)
2. `{exe_dir}/spratprofiles.cfg` (portable install)
3. User config:
   - Linux: `$XDG_CONFIG_HOME/sprat/spratprofiles.cfg` (default `~/.config/sprat/`)
   - macOS: `~/Library/Application Support/sprat/spratprofiles.cfg`
   - Windows: `%APPDATA%\sprat\spratprofiles.cfg`
4. `/usr/local/share/sprat/spratprofiles.cfg` (global)

### Profile format

```ini
[profile my_profile]
mode=compact
optimize=gpu
padding=2
max_width=2048
max_height=2048
trim_transparent=true
rotate=true
preset=quality
sort=stable:area
incremental=true

# Also accepts pipeline options (for GUIs)
dilate=1
zopfli=true
image_quality=90
```

Command-line flags override profile values. Unknown keys are errors. Deduplication is
configured with `--deduplicate`, `--dedup-threshold` and `--dedup-tolerance`; it is not a
profile key.

## Layout caching

Automatic caching in system temp directory:
- Linux/macOS: `$TMPDIR/sprat-<uid>/`
- Windows: `%TEMP%\sprat\`

Cache keys include every option that affects output. Entries older than 1 hour are pruned. New `sprat-cli` versions never reuse old cache entries. Archive inputs are not cached.

On Linux/macOS, cache is skipped if the directory is not owned by you or is writable by others.

## Duplicate detection details

### Exact deduplication
```sh
./build/spratlayout ./frames --deduplicate exact > layout.txt
```

Compares content hash; only pixel-identical sprites merged.

### Perceptual deduplication
```sh
./build/spratlayout ./frames --deduplicate perceptual \
  --dedup-threshold 5 \
  --dedup-tolerance 8 \
  > layout.txt
```

Uses perceptual hash (dHash) to find candidate pairs. Candidates are compared pixel-by-pixel; merged only if every RGBA channel is within tolerance. Fully transparent pixels in both sprites are ignored.

Each duplicate is compared against its group's canonical sprite, preventing differences from adding up along chains.

Layout output shows aliases:
```
sprite "original.png" rect:0,0,32,32 trim:4,10,4,6
alias "original.png" -> "duplicate.png"
alias "original.png" -> "moved.png" trim:4,4,4,12
```

An alias with different trim carries its own `trim:` so engines restore original placement.

### Limitations
- Sprites with different nine-slice insets are never merged
- Animation frames with small changes (e.g., a blink) stay separate
- Palette swaps stay separate

## Multipacking

When sprites don't fit in a single atlas, split across multiple:

```sh
./build/spratlayout ./frames --max-width 512 --max-height 512 --multipack > layout.txt
```

Layout output shows multiple `atlas` lines. `spratpack` then generates multiple PNG files:

```sh
./build/spratpack -a atlas_%d.png < layout.txt
# Outputs: atlas_0.png, atlas_1.png, ...
```

Extract a specific atlas:
```sh
./build/spratpack --atlas-index 1 < layout.txt > second_atlas.png
```

Each `spratconvert` format handles multiple atlases appropriately.

## Input filtering

### .spratlayoutignore
Directory inputs honor `.spratlayoutignore` (like `.gitignore`):
```
# Skip one test image
test_sprite.png

# Skip a folder
ignored_folder/
```

### List file exclusions
```sh
# myfiles.txt with exclude entries
sprite_001.png
sprite_002.png
exclude "test_sprites/excluded.png"
sprite_003.png
```

Use with `--stdin-list < myfiles.txt`.

[See spratlayout guide →](../guides/spratlayout.md) | [Back to README](../../README.md)
