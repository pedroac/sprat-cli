# spratlayout: Generate layouts

`spratlayout` analyzes your sprite images and calculates where each one should go in the atlas. It outputs a text layout that feeds into `spratpack` and `spratconvert`.

## Basic usage

```sh
# Simplest case: scan a folder of images
./build/spratlayout ./frames > layout.txt

# Scan a tar archive (supports .tar.gz, .tar.bz2, .tar.xz too)
./build/spratlayout sprites.tar.gz > layout.txt

# Pass images from a file list or stdin
ls *.png | ./build/spratlayout --stdin-list > layout.txt
```

## Common options

| Option | What it does |
|---|---|
| `--trim-transparent` | Remove empty borders from sprites to save space |
| `--padding N` | Add N pixels between sprites to prevent texture bleeding (default: 0) |
| `--extrude N` | Repeat edge pixels N times (padding is raised to at least `2 × N` automatically) |
| `--rotate` | Allow 90° rotation for tighter packing |
| `--scale F` | Pre-scale images (0 < F ≤ 1, e.g., 0.5 for half size) |
| `--deduplicate exact` | Merge pixel-identical sprites (saves atlas space) |
| `--multipack` | Split across multiple atlases if needed |

## Typical workflows

### Mobile game (small, optimized)
```sh
./build/spratlayout ./frames \
  --trim-transparent \
  --padding 2 \
  --deduplicate exact \
  > layout.txt
```

### Desktop game (GPU-friendly)
```sh
./build/spratlayout ./frames \
  --mode compact \
  --optimize gpu \
  --padding 2 \
  --extrude 1 \
  > layout.txt
```

### Power-of-two (console/WebGL)
```sh
./build/spratlayout ./frames \
  --mode pot \
  --max-width 2048 \
  --max-height 2048 \
  --padding 2 \
  > layout.txt
```

### Batch processing at a target resolution
```sh
./build/spratlayout ./frames \
  --source-resolution 3840x2160 \
  --target-resolution 1920x1080 \
  --padding 2 \
  > layout.txt
```

## Packing modes

- **`fast`** (default): Quick, good-enough packing using shelf algorithm. Best for large datasets.
- **`compact`**: Better utilization, slower. Good for mobile and console.
- **`pot`**: Forces power-of-two dimensions (512×512, 1024×512, etc.).
- **`grid`**: Places every sprite in a uniform cell, left-to-right and top-to-bottom. All sprites must have the same size; use `compact` for mixed sizes.

## Profiles

Save your favorite settings in a profile and reuse them:

**~/.config/sprat/spratprofiles.cfg**
```ini
[profile mobile]
mode=compact
optimize=space
padding=2
trim_transparent=true

[profile console]
mode=pot
max_width=2048
max_height=2048
padding=2
```

Use it:
```sh
./build/spratlayout ./frames --profile mobile > layout.txt
```

## What's next?

- Generated `layout.txt` → Pass to `spratpack` to create the image
- Generated `layout.txt` → Pass to `spratconvert` to create metadata

[See spratlayout reference →](../reference/spratlayout.md) | [Back to README](../../README.md)
