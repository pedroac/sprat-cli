# spratconvert: Complete Reference

`spratconvert` transforms a layout into metadata for game engines and other formats.

## All options

### Transform selection

- `--transform NAME|PATH` — Built-in format name or path to `.jsonnet` file
  - Built-in: `json`, `xml`, `csv`, `raw`, `css`, `godot`, `phaser-hash`, `phaser-array`, `phaser-anims`, `aseprite`, `plist`, `libgdx`, `unity` (group), `unity.json`, `unity.meta`, `unity.anim`
  - Custom: `--transform ./my-transform.jsonnet`
- `--list-transforms` — List all built-in formats with descriptions
- `--list-transforms-json` — List built-in formats as JSON
- `--transforms-dir` — Print the transforms directory and exit

### Output specification

- `-a PATTERN` — Atlas filename pattern (e.g., `-a atlas_%d.png`); aliases: `-o`, `--output`
  - Used for determining texture keys and GUIDs
  - Essential for multipack layouts
- `--image-path PATTERN` — Override path reference in metadata (same `%d` syntax)
  - By default, metadata refers to atlases exactly as `-a` names them
  - Use when metadata and images are in different directories
- `--output-dir DIR` — Write multiple files to directory (required for formats that generate per-file output)

### Metadata

- `--markers FILE` — Include markers (pivots, hitboxes, etc.)
- `--animations FILE` — Include named animations and sequences
- `--puppets FILE` — Include puppet rigs, poses, and sequences
- `--tiles FILE` — Include tile types, themes, variations and flags
- `--auto-animations` — Auto-detect animations from sprite filenames (e.g., `walk_01.png` → `walk` animation)

### Variants

## Built-in transforms

| Name | Extension | Output | Notes |
|---|---|---|---|
| `json` | `.json` | JSON | Generic sprite/animation/marker data |
| `raw` | `.sprat` | Text | Space-separated tokens for C/C++ (see [Raw format](../advanced/raw-format.md)) |
| `csv` | `.csv` | CSV | Spreadsheet-friendly format |
| `xml` | `.xml` | XML | Generic XML |
| `css` | `.css` | CSS | CSS sprite sheet |
| `aseprite` | `.json` | JSON | Aseprite JSON Array format; frame tags from animations |
| `libgdx` | `.atlas` | Text | LibGDX Atlas format; handles multipack |
| `godot` | `.json` | JSON | Godot SpriteFrames resource |
| `phaser-hash` | `.json` | JSON | Phaser 3 hash-keyed sprite sheet |
| `phaser-array` | `.json` | JSON | Phaser 3 array-keyed sprite sheet |
| `phaser-anims` | `.json` | JSON | Phaser 3 animation config only |
| `plist` | `.plist` | plist | Apple / TexturePacker format; one file per atlas for multipack |
| `unity.json` | `.json` | JSON | Unity Sprite data; one file per atlas for multipack |
| `unity.meta` | `.meta` | YAML | Unity texture metadata; one per atlas; requires `--output-dir` |
| `unity.anim` | `.anim` | YAML | Unity animation clips; one per animation; requires `--output-dir` |

### Multipack handling

| Format | Multipack output |
|---|---|
| `json`, `raw`, `xml`, `csv`, `css`, `libgdx` | One file covering every atlas |
| `phaser-hash`, `phaser-array` | One Phaser multi-atlas JSON (`textures: [...]`) for `this.load.multiatlas` |
| `godot` | One file with `atlases` list; each frame has `atlas` index |
| `aseprite`, `plist`, `unity.json` | One file per atlas (e.g., `atlas_0.plist`, `atlas_1.plist`); requires `--output-dir` |
| `unity.meta` | One `.meta` per atlas PNG (e.g., `atlas_0.png.meta`); requires `--output-dir` |
| `unity.anim` | Per-animation clips; each references its atlas GUID |

### Rotation support

- `rot` formats: `json`, `raw`, `xml`, `csv`, `css` — Report rotation in clockwise degrees (90 or 270)
- `rot-fixed cw`: `phaser-hash`, `phaser-array`, `aseprite`, `plist`, `godot`, `unity.json` — Fail with an error if the layout's rotation direction is not `cw`
- `rot-fixed ccw`: `libgdx` — Fail with an error unless the layout uses `--rotate-direction ccw`
- `no-rot`: `unity.meta` and the `unity` group — Refuse rotated sprites in either direction

## Sprite data (JSON output)

```json
{
  "sprites": [
    {
      "name": "hero_idle_01",
      "path": "./frames/hero_idle_01.png",
      "index": 0,
      "atlas_index": 0,
      "x": 10,
      "y": 20,
      "w": 64,
      "h": 64,
      "source_w": 64,
      "source_h": 64,
      "content_w": 64,
      "content_h": 64,
      "trim_left": 0,
      "trim_top": 0,
      "trim_right": 0,
      "trim_bottom": 0,
      "has_trim": false,
      "rotated": false,
      "has_slice": false,
      "has_pivot": false,
      "alias_of": ""
    }
  ],
  "atlases": [...],
  "animations": [...],
  "markers": [...]
}
```

## Animation data (JSON output)

```json
{
  "animations": [
    {
      "name": "hero_walk",
      "index": 0,
      "fps": 10,
      "duration": 0.8,
      "loop": "loop",
      "frame_indices": [1, 2, 3, 4, 5, 6],
      "frames": [
        {"name": "hero_walk_01", "index": 1},
        {"name": "hero_walk_02", "index": 2}
      ]
    }
  ]
}
```

Loop modes: `loop`, `once`, `pingpong`, `once-pingpong`

## Marker data (JSON output)

```json
{
  "markers": [
    {
      "name": "pivot",
      "type": "point",
      "index": 0,
      "sprite_index": 5,
      "x": 32,
      "y": 32
    },
    {
      "name": "hitbox",
      "type": "rectangle",
      "index": 1,
      "sprite_index": 5,
      "x": 10,
      "y": 10,
      "w": 44,
      "h": 44
    }
  ]
}
```

Marker types: `point`, `circle`, `rectangle`, `polygon`

## Custom Jsonnet transforms

Write your own `.jsonnet` file for custom output:

```jsonnet
local sprat = std.extVar("sprat");
{
  name: "my-format",
  description: "One line per sprite",
  extension: ".txt",
  content:
    std.join("\n", [
      "%s @ %d,%d (%dx%d)" % [s.name, s.x, s.y, s.w, s.h]
      for s in sprat.sprites
    ]) + "\n",
}
```

Use:
```sh
./build/spratconvert --transform ./my-format.jsonnet < layout.txt > output.txt
```

### Single-file output

Return `content` field with formatted string:

```jsonnet
{
  name: "compact",
  extension: ".txt",
  content: sprat.sprite_count + " sprites\n"
}
```

### Multi-file output

Return `files` array; requires `--output-dir`:

```jsonnet
{
  name: "per-sprite",
  extension: ".txt",
  files: [
    {
      filename: s.name + ".txt",
      content: s.name + ": " + s.w + "x" + s.h + "\n"
    }
    for s in sprat.sprites
  ]
}
```

Use:
```sh
./build/spratconvert --transform ./per-sprite.jsonnet --output-dir ./out < layout.txt
```

An empty `files` array is allowed and writes nothing (for example, a transform that emits
one file per animation when the layout has no animations).

### Jsonnet reference data

Available via `std.extVar("sprat")`:

**Global fields:**

| Field | Type | Description |
|---|---|---|
| `sprites` | array | All sprites across all atlases |
| `atlases` | array | Atlas entries with `index`, `width`, `height`, `path`, `stem`, `filename`, `hash_hex` (FNV-1a), `sprites` |
| `animations` | array | Animation definitions |
| `markers` | array | All markers with `sprite_index` (−1 for global) |
| `atlas_width`, `atlas_height` | number | First atlas dimensions |
| `atlas_path`, `atlas_stem` | string | First atlas path and stem |
| `texture_key` | string | Texture key (atlas stem for single; for multipack, stem without `%d`) |
| `atlas_count` | number | Total atlas count |
| `multipack` | boolean | True if multipack |
| `rotation_direction` | string | `cw` or `ccw` |
| `has_rotated` | boolean | True if any sprite is rotated |
| `scale`, `extrude` | number | Layout-level scale and extrusion |
| `scale_text` | string | Scale as text (up to 8 sig digits) |
| `puppets`, `has_puppets`, `puppet_count` | object/bool/number | Puppet rigs (if included) |
| `tiles`, `has_tiles`, `tile_count` | array/bool/number | Tile types (if included) |
| `tile_flags` | array | Flag table: each `{index, name}` maps a `flags_bitmask` bit to its flag |
| `has_animations`, `has_markers` | boolean | Whether animations/markers present |
| `animation_count`, `marker_count`, `sprite_count` | number | Counts |
| `output_stem`, `output_stem_hash_hex` | string | Variant name and FNV-1a hash |
| `output_pattern_hash_hex` | string | Hash of `-a` pattern (stable across transforms) |
| `output_pattern` | string | The `-a` pattern as given (empty without one) |
| `fps` | number | Default animation speed |
| `animations_path`, `markers_path`, `puppets_path`, `tiles_path` | string | Data file paths |

**Per sprite (`sprites[]`):**

| Field | Description |
|---|---|
| `name`, `path`, `index`, `atlas_index` | Identity |
| `atlas_path` | string | Path of the atlas image this sprite belongs to |
| `atlas_width`, `atlas_height` | number | Dimensions of that atlas |
| `x`, `y`, `w`, `h` | Packed rectangle (atlas pixels) |
| `content_w`, `content_h` | Dimensions accounting for rotation |
| `source_w`, `source_h` | Original size including trim |
| `trim_left`, `trim_top`, `trim_right`, `trim_bottom`, `has_trim` | Trim margins (atlas pixels) |
| `rotated` | True if packed rotated 90° |
| `has_slice`, `slice_left`, `slice_top`, `slice_right`, `slice_bottom` | Nine-slice data |
| `slice_h`, `slice_v` | Per-axis fill: `stretch`, `repeat`, `mirror`, `fixed` |
| `unity_y` | Y-up coordinate for Unity (`atlas_height - y - h`) |
| `has_pivot`, `pivot_x`, `pivot_y` | Pivot point (atlas pixels) |
| `pivot_x_norm`, `pivot_y_norm` | Normalized pivot (0–1, Y-up) |
| `pivot_rect_x_norm`, `pivot_rect_y_norm` | Normalized relative to trimmed rect (Y-up) |
| `name_hash_hex`, `name_hash_decimal`, `name_hash_signed` | FNV-1a hashes (different formats) |
| `name_css` | CSS-safe identifier |
| `alias_of` | Canonical sprite path (for deduplicated aliases) |
| `markers` | Array of marker objects for this sprite |

**Per animation (`animations[]`):**

| Field | Description |
|---|---|
| `name`, `index`, `fps`, `duration` | Animation identity (duration = one forward pass) |
| `loop` | `loop`, `once`, `pingpong`, `once-pingpong` |
| `frame_indices` | Global sprite indices |
| `frames` | Resolved frames with `index`, `name`, hashes |
| `is_alias`, `alias_source` | Alias support |

**Per puppet (`puppets[]`):**

| Field | Description |
|---|---|
| `name`, `index` | Puppet identity |
| `parts` | Parts in draw order, back first: `{index, name, parent, has_position, x, y, variants}` |
| `parts[].variants` | `{index, name, kind, sprite_index, animation_index}` (`kind`: `sprite` or `animation`) |
| `joints` | `{index, child, parent, child_marker, parent_marker, leeway, has_angle, angle_min, angle_max}` |
| `poses` | Pose definitions with part variants and transforms |
| `sequences` | Animation sequences with timing and easing |

**Per tile (`tiles[]`):**

| Field | Description |
|---|---|
| `name`, `index` | Tile identity |
| `code` | Single alphanumeric tile code |
| `layers` | Layer IDs for editors (does not affect rules) |
| `flags_bitmask` | Bits of the flags set on this tile (see `tile_flags`) |
| `themes` | Themes, each `{index, name, variations}` |
| `themes[].variations` | `{index, name, sprite_index, sprite_name, sprite_path, rules}` |

### Shared helpers

Import from `sprat.libsonnet`:

```jsonnet
local lib = import "sprat.libsonnet";
```

Available functions:
- `lib.format_double(v)` — Format float like C's `%.8g`
- `lib.consecutive_runs(indices)` — Split index array into contiguous ranges `[{from, to}]`
- `lib.safe_filename(name)` — Sanitize name to valid filename

## Transform search paths

User custom transforms are searched in:
1. `{exe_dir}/transforms/` (portable install)
2. User data dir:
   - Linux: `$XDG_DATA_HOME/sprat/transforms/` (default `~/.local/share/sprat/transforms/`)
   - macOS: `~/Library/Application Support/sprat/transforms/`
   - Windows: `%APPDATA%\sprat\transforms\`
3. `/usr/local/share/sprat/transforms/` (global)

## Pivot points

Define anchor points for sprites using markers.

### Per-sprite pivot

Add marker named `pivot` of type `point` to specific sprite:

```txt
path "./frames/hero.png"
- marker "pivot" point 32,64
```

### Global default pivot

Add marker named `pivot` without a `path`:

```txt
- marker "pivot" point 16,16

path "./frames/hero.png"
- marker "pivot" point 32,64
```

Marker coordinates are in original image pixels. Exported metadata is in atlas pixels (scaled by layout's scale factor).

Sprites without a pivot get no pivot in formats where pivot overrides engine default (`phaser-*`, `unity.json`). `unity.meta` uses center of original image.

## Automatic animations

Auto-detect animations from sprite filenames:

```sh
./build/spratconvert --transform json --auto-animations < layout.txt > layout.json
```

Sprite filenames like:
- `hero_walk_01.png`, `hero_walk_02.png` → animation `hero_walk` (frames 1–2)
- `hero_idle_01.png`, `hero_idle_02.png` → animation `hero_idle` (frames 1–2)

Naming convention: `{animation_name}_{frame_number}.png` where frame number is zero-padded.

[See spratconvert guide →](../guides/spratconvert.md) | [Back to README](../../README.md)
