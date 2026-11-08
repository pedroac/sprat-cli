# Markers & Animations

Add metadata to your sprites: pivot points, hitboxes, and animation sequences.

## Markers (pivot points & hitboxes)

Markers define named points and shapes on sprites (pivot points, hitboxes, spawn points, etc.).

Create `markers.txt`:
```txt
# Global pivot point (for all sprites without their own)
- marker "pivot" point 16,16

# Sprite-specific markers
path "./frames/hero.png"
- marker "pivot" point 32,64
- marker "foot_l" point 24,96
- marker "foot_r" point 40,96
- marker "hitbox" rectangle 20,40 24,40
```

Use with `spratconvert`:
```sh
./build/spratconvert --transform json --markers markers.txt < layout.txt > layout.json
```

### Marker types

| Type | Example | Use |
|---|---|---|
| `point` | `point 16,16` | Pivot, spawn point, attachment |
| `circle` | `circle 16,16 8` | Collision, light radius |
| `rectangle` | `rectangle 10,10 20,30` | Bounding box, interaction area |
| `polygon` | `polygon 0,0 32,0 32,32 0,32` | Irregular collision shape |

## Animations

Group sprites into named animations with timing and loop modes.

Create `animations.txt`:
```txt
fps 12

animation "idle"
- frame idle_01..idle_04

animation "run" 8 pingpong
- frame run_01..run_08

animation "jump"
- frame jump_01 jump_02 jump_03
```

Use with `spratconvert`:
```sh
./build/spratconvert --transform json --animations animations.txt < layout.txt > layout.json
```

### Options

| Option | What it does |
|---|---|
| `fps N` | Default playback speed (frames per second) |
| `animation "NAME" [FPS] [LOOP_MODE]` | Define animation |
| `- frame FRAME_LIST` | List of sprite paths, names, or indices |

### Ranges

```txt
animation "walk"
- frame walk_01..walk_08    # Ranges by name/index (includes end)
- frame "frame 1.png".."frame 4.png"  # Works with quoted names too
- frame 5..2               # Reverse ranges work
- frame walk_1..walk_3 idle_1..idle_2 # Mix ranges and singles
```

### Loop modes

| Mode | Behavior |
|---|---|
| `loop` (default) | Repeats the animation |
| `once` | Plays once, stops on last frame |
| `pingpong` | Forward then backward, repeats |
| `once-pingpong` | Forward then backward, stops |

### Example

```txt
root "./frames"
fps 10

animation "walk_left" 12 loop
- frame walk_left_1..walk_left_8

animation "walk_right" alias "walk_left" flip h

animation "fall" once
- frame fall_1 fall_2 fall_3
```

## Auto-detected animations

Skip the file and let `spratconvert` detect animations from sprite filenames:

```sh
./build/spratconvert --transform json --auto-animations < layout.txt > layout.json
```

Sprite filenames like:
- `walk_01.png`, `walk_02.png` → `walk` animation
- `idle_1.png`, `idle_2.png` → `idle` animation

The suffix is a trailing number, with or without zero-padding (`walk_1.png` and
`walk_01.png` both group under `walk`).

## Usage in JSON output

With markers and animations:
```json
{
  "sprites": [...],
  "animations": [
    {
      "name": "walk",
      "fps": 12,
      "loop": "loop",
      "frames": [
        {"name": "walk_01", "index": 1},
        {"name": "walk_02", "index": 2}
      ]
    }
  ],
  "markers": [
    {
      "name": "pivot",
      "type": "point",
      "sprite_index": 5,
      "x": 32,
      "y": 64
    }
  ]
}
```

## Aliases

Reference another animation with optional flipping:

```txt
animation "walk_left" alias "walk" flip h
animation "idle_mirrored" alias "idle" flip v
```

Flip options: `h` (horizontal), `v` (vertical), `hv` (both)

[See advanced data-files →](../advanced/data-files.md) | [Back to README](../../README.md)
