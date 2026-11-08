# spratpack: Create atlas images

`spratpack` reads a layout and generates the sprite sheet image(s). For single atlases it outputs PNG; for multiple atlases it outputs a TAR stream.

## Basic usage

```sh
# Generate a sprite sheet from a layout
./build/spratpack < layout.txt > spritesheet.png

# Save multiple atlases (auto-numbered: atlas_0.png, atlas_1.png, ...)
./build/spratpack -a atlas_%d.png < layout.txt
```

## Common options

| Option | What it does |
|---|---|
| `-a FORMAT` | Output filename pattern (e.g., `-a atlas_%d.png`, `-a sheet.png`) |
| `--frame-lines` | Draw red rectangles around each sprite (useful for debugging) |
| `--line-width N` | Width of frame lines (default: 1) |
| `--line-color R,G,B[,A]` | Color of frame lines (default: 255,0,0,255 for red) |
| `--dilate N` | Bleed opaque pixels into transparent edges (prevents dark halos with GPU filtering) |
| `--threads N` | Parallel sprite processing |

## Quick examples

### With debugging visualization
```sh
./build/spratpack --frame-lines --line-width 2 --line-color 0,255,0 < layout.txt > debug.png
```

### With artifact reduction (recommended for GPU filtering)
```sh
./build/spratpack --dilate 1 < layout.txt > spritesheet.png
```

### Scaled output
```sh
./build/spratpack --output-scale 0.5 < layout.txt > spritesheet_half.png
```

### Palette swaps (variant atlases)
Create variants like `hero_red.png`, `hero_blue.png` alongside `hero.png`, then:
```sh
./build/spratpack --path-suffix _red -a atlas_red.png < layout.txt
./build/spratpack --path-suffix _blue -a atlas_blue.png < layout.txt
```

## What's next?

- Generated PNG → Use in your game/app
- Generated layout → Pass to `spratconvert` to create metadata (JSON, XML, Godot, etc.)

[See spratpack reference →](../reference/spratpack.md) | [Back to README](../../README.md)
