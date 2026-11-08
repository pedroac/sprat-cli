# Extracting sprites

Extract individual sprites from an existing sprite sheet.

## Basic usage

### Detect sprite boundaries (spratframes)
```sh
# Scan a sprite sheet and find sprite boundaries
./build/spratframes sheet.png > layout.txt
```

### Extract sprites (spratunpack)
```sh
# Extract all sprites to a folder
./build/spratunpack --output ./frames < layout.txt

# Or pipe detection + extraction
./build/spratframes sheet.png | ./build/spratunpack --output ./frames
```

## Common options

**spratframes:**

| Option | What it does |
|---|---|
| `--algorithm shrink \| grid` | Detection method (default: `shrink`) |
| `--threshold N` | Alpha threshold for transparency (0–255, default: 1) |
| `--scale F` | Pre-scale output (0.0–1.0) |

**spratunpack:**

| Option | What it does |
|---|---|
| `--output DIR` | Where to save extracted sprites (required) |
| `--index-digits N` | Pad sprite names with zeros (default: 4 digits) |
| `--threads N` | Parallel extraction |

## Examples

### Extract with adjusted transparency threshold
```sh
./build/spratframes --threshold 128 sheet.png | \
./build/spratunpack --output ./frames
```

### Extract at 50% scale
```sh
./build/spratframes --scale 0.5 sheet.png | \
./build/spratunpack --output ./frames_half
```

### Extract a specific sprite only
```sh
# Get layout, then extract just one sprite by index
./build/spratframes sheet.png | \
./build/spratunpack --output ./frames --index 5
```

## What's next?

- Recovered sprites can be re-packed with `spratlayout`
- Use extracted frames to rebuild or modify the atlas

[Back to README](../../README.md)
