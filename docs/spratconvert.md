# spratconvert: engine metadata

`spratconvert` turns a layout into metadata for game engines and other formats.

`spratconvert` reads layout text from stdin and writes transformed output to stdout.
Transforms are [Jsonnet](https://jsonnet.org/) files that receive the full layout data and produce any text format for your game engine or pipeline.

List built-in transforms:

```sh
./build/spratconvert --list-transforms
```

Use a built-in transform:

```sh
./build/spratconvert --transform json < layout.txt > layout.json
```

Provide `--atlas` so atlas paths are deterministic in multi-atlas layouts:

```sh
./build/spratconvert --transform json --atlas atlas_%d.png < layout.txt > layout.json
```

## Image paths inside the metadata

By default, the metadata refers to each atlas image exactly as `--atlas` names it. Engines (Cocos2d-x, libGDX, Phaser, Unity, ...) usually resolve that reference relative to the metadata file, so a directory in `--atlas` ends up repeated: with `--atlas out/atlas_%d.png --output-dir out`, the files land in `out/`, but they point to `out/atlas_0.png`, i.e. `out/out/atlas_0.png` from the metadata's point of view.

Use `--image-path PATTERN` (same `%d` syntax) to choose the reference written into the metadata. `--atlas` still names the per-atlas output files and seeds stable IDs such as Unity GUIDs; without `--atlas`, `--image-path` names them too.

```sh
./build/spratconvert --transform plist --atlas out/atlas_%d.png --image-path atlas_%d.png \
  --output-dir out < layout.txt
# out/atlas_0.plist refers to "atlas_0.png"
```

## Automatic Animations
Automatically group sprites into animations based on their filenames (e.g., `hero_walk_01.png`, `hero_walk_02.png` -> animation `hero_walk`).
```sh
./build/spratconvert --transform json --auto-animations < layout.txt > layout.json
```

## Pivot Points
Define pivot points (anchors) for sprites using markers.
1.  **Per-sprite pivot**: Add a marker named `pivot` of type `point` to a specific sprite.
2.  **Global pivot**: Add a marker named `pivot` of type `point` without a `path`.

`spratconvert` resolves pivot positions and exposes them as `has_pivot`, `pivot_x`, `pivot_y`, `pivot_x_norm`, `pivot_y_norm`, `pivot_y_norm_raw`, `pivot_rect_x_norm` and `pivot_rect_y_norm` on each sprite object.

Marker coordinates (pivots, hitboxes, ...) are in the original image's pixels, so they don't change when you pack at another `--scale`. Exported metadata is in atlas pixels: `spratconvert` scales markers, trims and nine-slice insets by the layout's `scale`, the way the packed sprite size already is. For nine-patch (`.9.png`) images, the original image is the content inside the 1px marker border.

Sprites without a pivot marker get no pivot in formats where a pivot overrides the engine default (`phaser-*`, `unity.json`), and `unity.meta` uses the center of the original image.

Example `markers.txt`:
```txt
# Global default pivot
- marker "pivot" point 16,16

path "./frames/hero.png"
# Specific pivot for hero
- marker "pivot" point 32,64
```

Optional extra data files:

```sh
./build/spratconvert --transform json --markers markers.txt --animations animations.txt < layout.txt > layout.json
./build/spratconvert --transform raw --markers markers.txt --animations animations.txt --puppets puppets.txt < layout.txt > layout.sprat
```

## Transform search paths

Transform files are searched in:
1. `{exe_dir}/transforms/` (beside the executable, portable install)
2. User data dir:
   - Linux: `$XDG_DATA_HOME/sprat/transforms/` (default `~/.local/share/sprat/transforms/`)
   - macOS: `~/Library/Application Support/sprat/transforms/`
   - Windows: `%APPDATA%\sprat\transforms\`
3. `/usr/local/share/sprat/transforms/` (Global)

## Built-in transforms

| Name | Output | Notes |
|------|--------|-------|
| `json` | JSON | Generic metadata: sprites, atlases, animations, markers (global markers in a top-level `markers` list) |
| `raw` | Text | The same metadata as `json` as plain space-separated tokens, for loading from C in one pass with no parser library (used by [sprat-lib](https://github.com/pedroac/sprat-lib)); see [Raw format](raw-format.md) |
| `csv` | CSV | Flat spreadsheet-friendly list; animation rows are `animation,INDEX,NAME,FPS,FRAMES,FLIP,LOOP` (`FRAMES` separated by `\|`) |
| `xml` | XML | Generic XML |
| `css` | CSS | CSS sprite sheet |
| `aseprite` | JSON | Aseprite JSON Array format; frameTags built from animations (non-contiguous animations become multiple tags); one file per atlas for multipack |
| `libgdx` | Text | LibGDX Atlas format; handles multipack; rotated sprites require `--rotate-direction ccw` |
| `godot` | JSON | Godot SpriteFrames resource; each animation lists its `frames` in play order (`from`/`to` only when they form one ascending run); multipack adds `atlases` and a per-frame `atlas` index |
| `phaser-hash` | JSON | Phaser 3 hash-keyed sprite sheet; multi-atlas JSON for multipack |
| `phaser-array` | JSON | Phaser 3 array-keyed sprite sheet; multi-atlas JSON for multipack |
| `phaser-anims` | JSON | Phaser 3 animation config; frames use `texture_key` (e.g. `atlas` for a multipack `atlas_%d.png`) |
| `plist` | plist | Apple / TexturePacker plist; one file per atlas for multipack |
| `unity` | Group | `unity.json` + `unity.meta` + one `unity.anim` per animation (file named after it, with path characters and a leading dot replaced by `_`, and a `_` prefix for Windows device names such as `con`); the clips reference the texture GUID of the `.meta`, which comes from the `-a` name, so pass `-a atlas.png` (also naming the file `atlas.png.meta`) to keep the textures of one project apart; requires `--output-dir`; multipack writes one `.json` and one `<atlas>.meta` per atlas; rotated sprites are not supported (Unity sprite rects have no rotation); in `unity.meta` the pivot and nine-slice border are relative to the trimmed sprite rect |

Markers, animations and puppets are described in [Markers, animations and puppets](data-files.md). The `raw` format is described in [Raw format](raw-format.md).

## Transform format

Each transform is a Jsonnet file that evaluates to a JSON object:

- `name` — display name
- `description` — shown by `--list-transforms`
- `extension` — output file extension (e.g. `".json"`)
- `content` — string output for a single file
- `filename` — optional, with `content`: the file name used with `--output-dir` instead of `{variant}{extension}` (stdout output ignores it); `unity.meta` uses it to write `<atlas>.meta` when `-a` names the atlas
- `files` — array of `{filename, content}` for multi-file output; mutually exclusive with `content`, requires `--output-dir`. Each `filename` must be a unique plain file name that is valid on every platform: no `/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|` or `..`, no trailing dot or space, and no Windows device name such as `CON`, `NUL`, `COM1` or `LPT1` (with or without an extension); otherwise nothing is written. `lib.safe_filename(name)` in `sprat.libsonnet` turns arbitrary names into valid ones

The layout data is available as `std.extVar("sprat")`:

```jsonnet
local sprat = std.extVar("sprat");
```

**Global fields:**

| Field | Type | Description |
|---|---|---|
| `sprites` | array | All sprites across all atlases |
| `atlases` | array | Each entry has `index`, `width`, `height`, `path`, `stem`, `filename`, `hash_hex` (FNV-1a of `path`), `sprites` |
| `animations` | array | Animation definitions |
| `markers` | array | Every marker, each with its `sprite_index` (`-1` for a global marker, given before any `path` in the markers file) |
| `atlas_width`, `atlas_height` | number | First atlas dimensions |
| `atlas_path`, `atlas_stem` | string | First atlas path and stem |
| `texture_key` | string | Key for the whole atlas set: `atlas_stem` for one atlas; for multipack, the `-a` pattern's stem without `%d` (`atlas_%d.png` → `atlas`) |
| `atlas_count` | number | Total atlas count |
| `multipack` | boolean | `true` when layout declares `multipack true` |
| `rotation_direction` | string | `cw` or `ccw` (from the layout's `rotation` line; default `cw`) |
| `has_rotated` | boolean | `true` when any sprite is rotated |
| `scale`, `extrude` | number | Layout-level values |
| `scale_text` | string | `scale` as text, up to 8 significant digits (`0.1`); use it where a transform writes the scale as a string, since Jsonnet turns `sprat.scale` into `0.10000000000000001` |
| `puppets` | array | Puppet rigs (see below) |
| `has_animations`, `has_markers`, `has_puppets` | boolean | Whether there are animations, markers or puppets |
| `animation_count`, `marker_count`, `puppet_count`, `sprite_count` | number | Counts |
| `output_stem`, `output_stem_hash_hex` | string | Output stem (the transform's variant name with `--output-dir`) and its FNV-1a hex hash |
| `output_pattern_hash_hex` | string | FNV-1a hex hash of the `-a` pattern (of `""` without one): the same for every transform of a run, so IDs that one output refers to in another (Unity texture GUIDs) match |
| `animations_path`, `markers_path`, `puppets_path` | string | Paths to the extra files |

**Per sprite (`sprites[]`):**

| Field | Description |
|---|---|
| `index`, `name`, `path`, `atlas_index` | Identity |
| `x`, `y`, `w`, `h` | Packed rectangle in the atlas |
| `content_w`, `content_h` | Dimensions accounting for rotation |
| `source_w`, `source_h` | Original size including trim margins |
| `trim_left`, `trim_top`, `trim_right`, `trim_bottom`, `has_trim` | Trim margins in atlas pixels (scaled; a nine-patch's marker border excluded) |
| `rotated` | `true` when packed rotated 90° (direction given by `rotation_direction`) |
| `has_slice` | `true` when nine-slice data is present |
| `slice_left`, `slice_top`, `slice_right`, `slice_bottom` | Nine-slice insets (only present when `has_slice` is true) |
| `slice_h`, `slice_v` | Per-axis fill mode: `stretch`, `repeat`, `mirror`, or `fixed` (only present when `has_slice` is true) |
| `unity_y` | `atlas_height - y - h` (Y-up coordinate for Unity) |
| `has_pivot` | `true` when a per-sprite or global pivot marker applies |
| `pivot_x`, `pivot_y` | Pivot in atlas pixels from marker lookup (0 if not set) |
| `pivot_x_norm`, `pivot_y_norm` | Normalized; `pivot_y_norm` is Y-up (Unity convention) |
| `pivot_y_norm_raw` | Normalized Y-down |
| `pivot_rect_x_norm`, `pivot_rect_y_norm` | Normalized relative to the trimmed rect (`x`, `y`, `content_w`, `content_h`); `pivot_rect_y_norm` is Y-up |
| `name_hash_hex` | 16-char FNV-1a hex string |
| `name_hash_decimal` | FNV-1a as a decimal string (serialized as JSON string to avoid float precision loss) |
| `name_hash_signed` | Same hash as a signed 64-bit decimal string, for IDs stored as `int64` (Unity `internalID`/`fileID`) |
| `name_css` | CSS-safe identifier |
| `alias_of` | Canonical sprite path for a deduplicated alias; empty for regular sprites |
| `markers` | Array of marker objects attached to this sprite |

**Per animation (`animations[]`):**

| Field | Description |
|---|---|
| `index`, `name`, `fps`, `duration` | Identity and timing (`duration` is one forward pass) |
| `loop` | `loop`, `once`, `pingpong` or `once-pingpong`; an alias has its source's |
| `frame_indices` | Global sprite index sequence (may be non-contiguous) |
| `frames` | `[{index, name, name_hash_decimal, name_hash_signed, name_hash_hex}]` resolved per frame |
| `is_alias`, `alias_source`, `flip` | Alias support |

**Per puppet (`puppets[]`):**

| Field | Description |
|---|---|
| `index`, `name` | Identity |
| `parts` | Parts in draw order, back first: `{index, name, parent, parent_name, has_position, x, y, variants}`; `parent` is the index of the part it is attached to, or `-1` (and `parent_name` empty); a part that isn't attached has `has_position` and `x`, `y` in atlas pixels |
| `parts[].variants` | `{index, name, kind, sprite_index, animation_index}`; `kind` is `sprite` or `animation`, and the other index is `-1` |
| `joints` | `{index, child, child_name, child_marker, parent, parent_name, parent_marker, leeway, has_angle, angle_min, angle_max}`; `child` and `parent` are part indexes, `leeway` is in atlas pixels; plus `is_constraint`, true for a part's further attachments |
| `poses` | `{index, name, parts}`; each of `parts`, in part order, is `{part, part_name, variant, variant_name, has_angle, angle, has_offset, offset_x, offset_y, has_position, x, y, has_scale, scale_x, scale_y, has_opacity, opacity, has_tint, tint_r, tint_g, tint_b}`, with `variant` `-1` (and `variant_name` empty) to keep it, offsets and positions in atlas pixels, scales as factors, `opacity` from 0 to 1 and tints from 0 to 255 per channel; a value without its flag has its rest value (0, scale 1, opacity 1, tint 255) |
| `sequences` | `{index, name, fps, loop, frame_count, duration, steps}`; `frame_count` adds up every step's `in` + `hold`, `duration` is it in seconds at `fps`, and each of `steps` is `{pose, pose_name, in, hold, ease, part_eases}` with `ease` `linear` or `step`; `part_eases`, in part order, lists the parts the pose sets that ease the other way, each `{part, part_name, ease}` |

**Per marker (`markers[]` and `sprite.markers[]`):**

| Field | Description |
|---|---|
| `index`, `name`, `type` | Identity; `type` is `point`, `circle`, `rectangle`, or `polygon` |
| `x`, `y`, `radius`, `w`, `h`, `vertices` | Geometry in atlas pixels, scaled from the marker file's original-image pixels (fields present depend on type) |
| `sprite_index`, `sprite_name`, `sprite_path` | Owning sprite |

## Shared helpers (`sprat.libsonnet`)

All transforms in the transforms directory can import shared helpers:

```jsonnet
local lib = import "sprat.libsonnet";
```

- `lib.format_double(v)` — formats a float like C's `%.8g` (works around a known Jsonnet v0.20 `%g` bug)
- `lib.consecutive_runs(indices)` — splits an index array into contiguous ranges `[{from, to}]`; used by the Aseprite transform to build frameTags from non-contiguous animations

## Custom transforms

A transform is any `.jsonnet` file. Pass a path directly to `--transform`:

```jsonnet
local sprat = std.extVar("sprat");
{
  name: "compact-log",
  description: "One line per sprite",
  extension: ".txt",
  content:
    "atlas=%dx%d sprites=%d\n" % [sprat.atlas_width, sprat.atlas_height, sprat.sprite_count] +
    std.join("\n", [
      "%d %s @ %d,%d %dx%d" % [s.index, s.path, s.x, s.y, s.w, s.h]
      for s in sprat.sprites
    ]) + "\n",
}
```

```sh
./build/spratconvert --transform ./my.jsonnet < layout.txt > output.txt
```

Multi-file output — return `files` instead of `content` and use `--output-dir`:

```jsonnet
local sprat = std.extVar("sprat");
{
  name: "one-per-anim",
  extension: ".txt",
  files: [
    { filename: anim.name + ".txt", content: anim.name + ": " + anim.fps + "fps\n" }
    for anim in sprat.animations
  ],
}
```

```sh
./build/spratconvert --transform ./per-anim.jsonnet --output-dir ./out < layout.txt
```

Sprite names default to the source file basename without extension (e.g. `./frames/run_01.png` becomes `run_01`).

[Back to the README](../README.md)
