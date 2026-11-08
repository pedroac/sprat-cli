# spratconvert: Create metadata

`spratconvert` takes a layout and generates metadata for your game engine or format. It outputs JSON, XML, Godot, Unity, Phaser, Aseprite, plist, libGDX, CSV, CSS, or custom formats.

## Basic usage

```sh
# See available built-in formats
./build/spratconvert --list-transforms

# Generate JSON metadata
./build/spratconvert --transform json < layout.txt > layout.json

# Generate for a specific engine
./build/spratconvert --transform godot < layout.txt > sprites.json
./build/spratconvert --transform unity --output-dir ./out -a atlas.png < layout.txt
./build/spratconvert --transform phaser-hash < layout.txt > sprites.json
```

## Common options

| Option | What it does |
|---|---|
| `--transform NAME` | Output format: `json`, `xml`, `csv`, `godot`, `unity`, `phaser-hash`, `plist`, `aseprite`, `libgdx`, `css`, `raw` |
| `-a PATTERN` | Atlas filename pattern (e.g., `-a atlas_%d.png` for multipack) |
| `--image-path PATTERN` | Path reference written in metadata (use when metadata and images are in different folders) |
| `--output-dir DIR` | Write multiple files to a directory (required for some formats) |
| `--auto-animations` | Detect animations from sprite filenames (e.g., `walk_01.png`, `walk_02.png` → `walk` animation) |
| `--markers FILE` | Include pivot points, hitboxes, etc. (see [Markers & Animations](markers-animations.md)) |
| `--animations FILE` | Include named animations (see [Markers & Animations](markers-animations.md)) |
| `--puppets FILE` | Include cutout puppet rigs, poses and sequences |

## Quick examples

### For Godot (single file)
```sh
./build/spratconvert --transform godot < layout.txt > sprites.json
```

### For Unity (with metadata files)
```sh
./build/spratconvert --transform unity --output-dir ./out -a atlas.png < layout.txt
# Creates: out/atlas.json, out/atlas.png.meta, and one .anim per animation
```

### For Phaser (with auto-detected animations)
```sh
./build/spratconvert --transform phaser-hash --auto-animations < layout.txt > sprites.json
```

### Multi-atlas with correct paths
```sh
./build/spratlayout ./frames --multipack | \
./build/spratconvert \
  --transform json \
  -a assets/atlas_%d.png \
  --image-path atlas_%d.png \
  > sprites.json
```

### With custom pivots
Create `markers.txt`:
```
- marker "pivot" point 16,16

path "./frames/hero.png"
- marker "pivot" point 32,64
```

Then:
```sh
./build/spratconvert --transform godot --markers markers.txt < layout.txt > sprites.tres
```

## Format guide

| Format | Best for | Notes |
|---|---|---|
| `json` | Generic, data-driven | Simple sprite and frame data |
| `xml` / `csv` | Spreadsheets, custom tools | Easy to parse in any language |
| `godot` | Godot Engine | SpriteFrames resource |
| `unity.json` | Unity (modern) | Requires `--output-dir` |
| `phaser-hash` | Phaser 3 | Hash-based texture keys |
| `plist` | Apple / TexturePacker-compatible | Legacy format |
| `aseprite` | Aseprite, animation tools | JSON Array format |
| `libgdx` | libGDX (Java) | Native format |
| `css` | Web (CSS sprites) | One-page websites |
| `raw` | C/C++ | Space-separated text tokens for a one-pass loader |

## What's next?

- Load the metadata in your game engine
- Combine with sprites (from `spratpack`) to display them
- Use animations and markers for gameplay logic

[See spratconvert reference →](../reference/spratconvert.md) | [Back to README](../../README.md)
