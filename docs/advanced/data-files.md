# Markers, Animations, Puppets & Tiles (Technical)

Complete technical specification for the data file formats that `spratconvert --markers`, `--animations`, `--puppets` and `--tiles` read.

For common use cases and simple examples, see [Markers & Animations guide](../guides/markers-animations.md).

## Markers

`--markers` expects a plaintext file using the `path` and `- marker` DSL. Every line must be a `root`, `path` or `- marker` line (or a `#` comment); anything else, a value that doesn't fit, or a second pivot for the same sprite is reported.
An optional `root` directive sets a base directory; `path` values that are relative are resolved against it (a sprite name such as `hero` still names that sprite).
Markers before any `path` are global. A `path` that matches no sprite is reported, and its markers are ignored rather than made global, so a mistyped path can't hand its pivot to every sprite. A marker with an unknown type or values that don't match its type is ignored with a warning.
Supported marker types:
- `point`: `x,y`
- `circle`: `x,y radius`
- `rectangle`: `x,y w,h`
- `polygon`: `x,y x,y ...` (list of vertices)

Example `markers.txt`:
```txt
root "./frames"
path "a.png"
- marker "hit" point 3,5
- marker "hurt" circle 6,7 4
path "b"
- marker "foot" rectangle 1,2 3,4
```

## Animations

`--animations` expects a plaintext file using the `animation` and `- frame` DSL. Every line must be a `root`, `fps`, `loop`, `animation` or `- frame` line (or a `#` comment); anything else is reported, and so is an animation with no frames. A `- frame` line lists one or more frames, each a sprite path, name or index: a bare number such as `1` is an index, so quote the path to name a sprite file such as `"1.png"`. A range lists several: `0..7` by index, `run_01..run_08` by name (both ends must differ only in their number, and the first end's zero padding is kept), `"idle 1.png".."idle 4.png"` with quotes. A range may count down (`7..0`). A frame that matches no sprite, or an index outside the layout, is ignored with a warning.
An optional `root` directive sets a base directory; quoted frame paths that are relative are resolved against it (sprite names such as `run_01` still name those sprites).
Animation names must be unique; with `--auto-animations`, an animation from the file replaces the automatic one of the same name.
The `fps` line sets the default speed for every animation that has none of its own, wherever the line appears in the file, and a number after an animation's name sets its own. Both go up to 100 frames per second, the limit [sprat-lib](https://github.com/pedroac/sprat-lib) enforces; a higher value is an error. A value of 0 or less means the default, 8. Only the leading integer is read, with a warning when anything follows it (`12.5` is read as 12); a value that does not start with a number, or a missing one, is ignored with a warning.

Each animation also has a loop mode: `loop` (the default) repeats it, `once` plays it once and stops on the last frame, `pingpong` plays it forward then backward and repeats, and `once-pingpong` plays forward then backward once and stops on the first frame. The `loop` line sets the default mode (also wherever it appears), and a mode after an animation's name sets its own, before or after its fps (`animation "run" 8 pingpong`). An alias plays the way the animation it replays does. An unknown mode is ignored with a warning. Formats with their own ping-pong use it (Phaser `yoyo`, Aseprite tag `direction` and `repeat`); Godot and Unity only have a loop flag, so their frame lists include the way back (`0 1 2 1` for `pingpong`, `0 1 2 1 0` for `once-pingpong`).

An alias replays another animation instead of listing frames, optionally mirrored: `animation "run-left" alias "run" flip h`. `flip` takes `h`, `v` or `hv`; any other value is ignored with a warning. An alias of an unknown animation, or a cycle of aliases, is an error.

Example `animations.txt`:
```txt
root "./frames"
fps 12
animation "run" 8 pingpong
- frame run_01..run_08
animation "run-left" alias "run" flip h
animation "idle"
- frame "a.png" b 1
```

## Puppets

`--puppets` expects a plaintext file describing puppets: cutout rigs in the spirit of Terry Gilliam's animations and paper stop motion. A puppet is a set of parts (face, mouth, eyes, ...) joined by their markers. Each part has interchangeable variants, sprites or animations (a smiling and an angry mouth, a blinking eye), and a joint is between parts, not variants, so any variant can be swapped in. `spratconvert` describes the rig, its poses and sequences; the engine plays them. Every line must be a `root`, `puppet`, `part`, `- sprite`, `- animation`, `attach`, `pose`, `- part`, `sequence` or `- pose` line (or a `#` comment); anything else is reported.

- `puppet "NAME"` starts a puppet. Puppet names must be unique.
- `part "NAME" [at:X,Y]` starts a part of the current puppet. Parts are drawn in file order, the first at the back. Part names must be unique within their puppet.
- Every part is placed one way: attached to another part (see `attach`), or by its own position `at:X,Y`, where its sprite's pivot goes in the puppet's space. That space's origin (0,0) is the puppet's pivot, with Y down, in original-image pixels like markers. Any number of parts may be positioned, so a puppet can be all free parts, one rig, or a rig plus free parts (a fly around a head). Moving the puppet moves them all. A torso at `at:0,-120` puts the pivot 120 pixels below it, between the feet; a caterpillar can position its middle segment and attach the others on both sides.
- `- sprite "SPRITE" [-> "NAME"]` adds a sprite variant to the current part: a sprite path (resolved against `root` like markers paths), name, or index (a bare number). `- animation "ANIMATION" [-> "NAME"]` adds an animation, from `--animations`, `--auto-animations` or the layout stream; an alias animation (a mirrored one) works too. A variant is named after its sprite or animation unless `-> "NAME"` names it; variant names must be unique within their part.
- `attach "CHILD":"MARKER" to "PARENT":"MARKER" [leeway:N] [angle:MIN..MAX]` attaches the child part to its parent part, anywhere in the puppet. The child's marker must be a `point`, in every sprite the child shows (each frame of an animation variant); the parent's marker may be any shape, in every sprite the parent shows, and its shape is where the child's point may be: on the point, or anywhere inside a circle, rectangle or polygon (an eye socket for a pupil). `leeway:N` lets the child's point drift up to `N` pixels beyond that (default 0, rigid). `angle:MIN..MAX` limits how far the child turns relative to its parent, in degrees clockwise (y down), between -360 and 360; without it the child turns freely. Leeways are in original-image pixels, like markers, and are scaled into atlas pixels.
- A part may be attached more than once (a stick held by both hands). Its first `attach` is its parent: the part it hangs from, whose angle its own is measured from. Further `attach` lines are constraints: they keep the part within that marker's shape and leeway too, which may close loops, and an editor (or an engine that wants live physics) honors them while posing. A positioned part has no parent, so all its `attach` lines are constraints; two positioned parts can't be attached, since one of them must be placed by the joint.
- `pose "NAME"` starts a pose of the current puppet, followed by its `- part "PART" [variant:NAME] [angle:DEGREES] [offset:X,Y] [at:X,Y] [scale:X[,Y]] [opacity:A] [tint:#RRGGBB]` lines. A pose sets only what it names, so poses layer (`walk` + `smile`): parts it doesn't set keep what they had. `variant:` shows one of the part's variants (quote a name with spaces: `variant:"run back"`). `angle:` turns the part from its rest angle, in degrees clockwise: relative to its parent, so children follow, and within its parent joint's `angle:` limits; or around its own pivot for a positioned part. `offset:` moves an attached part's point from the parent marker's `x,y` (the eye in its socket), which engines keep within the marker's shape and leeway. `at:` moves a positioned part. `scale:` stretches the part's sprite along its own axes, around its pivot (a positioned part) or its attaching point, before it turns: one factor for both axes (`scale:1.2`) or one per axis (`scale:1.2,0.8` for squash and stretch), between -16 and 16, where a negative factor flips it. `opacity:` fades it, from 0 (invisible) to 1. `tint:#RRGGBB` multiplies its colors (`#ffffff`, white, leaves them as they are; `#ff8080` reddens it). Scale, opacity and tint are the part's own: its children aren't scaled, faded or tinted, though a parent's scale moves its markers, and so where its children attach. Angles are whole degrees between -360 and 360; offsets and positions are in original-image pixels and are scaled. Pose names must be unique within their puppet. An editor works out the angles from where the user drags the parts, so playing a pose needs no solving.
- `sequence "NAME" [fps:N] [loop:MODE]` starts a sequence of poses, followed by its `- pose "POSE" [in:FRAMES] [hold:FRAMES] [ease:linear|step] [step:PARTS] [linear:PARTS]` steps. Timing is in frames, as stop-motion animators count them: at `fps` (default 8, up to 100, like animations; a higher value is an error), each step reaches its pose in `in` frames and holds it `hold` frames (both default 0, up to 100000; a step must last at least one frame). `ease:linear` (the default) tweens angles, offsets, positions, scales, opacities and tints (each channel) over `in`; `ease:step` keeps the old pose through `in`, then snaps, for pure stop motion. `step:` and `linear:` override that for some of the parts the step's pose sets, as a comma-separated list (`- pose "hello" in:6 step:"eyes",mouth` tweens the arm while the eyes and mouth snap; `- pose "rest" in:4 ease:step linear:arm` snaps all but the arm); an unknown part is an error, and a part the pose doesn't set, or one listed the way the whole step already eases, is reported and ignored. Variants never morph: they snap when the step's pose arrives, at the end of `in`. `loop` takes the animation loop modes (`loop`, the default, `once`, `pingpong`, `once-pingpong`). Only what a step's pose sets changes, so sequences on different parts combine (`walk` on the body, `talk` on the mouth); when two set the same part, the one applied later wins. When looping, the first step tweens from where the last left off; `pingpong` plays the steps back in reverse. A sequence at the fps of an animation variant (both 12 from Krita) stays in step with it frame by frame. Sequence names must be unique within their puppet.

A sprite that lacks a joint's marker, a part with no variants, a puppet with no parts, a pose that sets no part, a pose line that sets nothing, a sequence with no steps and a step that lasts no frame are reported. An attach line naming an unknown part, attaching a part to itself or two positioned parts, or closing a cycle of parents is an error, and so are a repeated puppet, part, variant or pose name, a part with neither a position nor a parent, and a pose that sets an unknown part or variant, sets a part twice, turns a part beyond its joint's limits, moves an attached part with `at:` or offsets a positioned one, and a sequence that plays an unknown pose or repeats a sequence name.

Example `puppets.txt`:
```txt
root "./pinocchio"
puppet "pinocchio"

part "face" at:0,-40
- sprite "face.png"

part "nose"
- sprite "nose-short.png" -> "short"
- sprite "nose-long.png" -> "long"

part "mouth"
- sprite "mouth-smile.png" -> "smile"
- sprite "mouth-angry.png" -> "angry"
- animation "talk"

part "eye"
- sprite "eye.png"

part "pupil"
- sprite "pupil.png"

attach "nose":"pivot" to "face":"nose" angle:-10..10
attach "mouth":"pivot" to "face":"mouth" leeway:2
attach "eye":"pivot" to "face":"eye"
attach "pupil":"pivot" to "eye":"socket"

part "fly" at:24,-70
- animation "buzz"

pose "angry"
- part "mouth" variant:"angry"
- part "nose" angle:-8
- part "face" tint:#ffb0b0

pose "look left"
- part "pupil" offset:-2,0
- part "fly" at:12,-75 scale:-1,1 opacity:0.6

sequence "fuss" fps:12 loop:pingpong
- pose "angry" in:3 hold:2
- pose "look left" in:2 hold:4 ease:step linear:"pupil"
```

Here the face is placed 40 pixels above the puppet's pivot, and the fly is free. `angry` turns the face red, and in `look left` the fly faces the other way, half see-through. In `fuss`, the fly jumps to its place in `look left` while the pupil slides there over two frames. `face.png` has `nose`, `mouth` and `eye` point markers, `eye.png` a `socket` circle, and every other sprite (each frame of `talk` too) a `pivot` point. For now, the `raw` transform writes puppets; custom transforms get them in `sprat.puppets`.

## Tiles

`--tiles` expects a plaintext file describing tile types for tile-based maps. A tile type has a one-character code, optional layers and flags, and one or more themes; each theme lists sprite variations, optionally with a neighbor rule. Every line must be a `root`, `flags`, `tile`, `theme` or `- variation` line (or a `#` comment); anything else is reported. A tiles file is written by hand, so problems are warnings that drop the offending line or tile rather than errors that stop the conversion.

- `flags NAME...` defines the flag names, in order: the first is bit 0, the next bit 1, and so on, up to 32. A repeated name or a flag beyond the limit is ignored with a warning. Define flags before the tiles that use them.
- `tile "NAME" code:C [layers:N,N,...] [flags:F,F,...]` starts a tile type. `code` is a single alphanumeric character and must be unique; a tile without exactly one valid code is ignored. `layers` lists the layer IDs the tile may appear on; it is for editors and does not affect rules. `flags` names flags defined by an earlier `flags` line; an unknown flag is ignored with a warning.
- `theme "NAME"` starts a theme of the current tile. Theme names must be unique within their tile; a duplicate is ignored with a warning.
- `- variation "SPRITE" [rules:CONSTRAINT]` adds a sprite variation to the current theme. `SPRITE` is a sprite path (resolved against `root` like markers paths) or a sprite name; quote a path with spaces. The colon form `- variation:"SPRITE"` is the same line. `rules` is free text describing the neighbor rule, such as `isolated` or `g[2,46]`; `spratconvert` stores it without interpreting it. A sprite that matches no sprite is ignored with a warning. Variation names must be unique within their theme; a duplicate is ignored with a warning. The first theme of a tile is its default.

Example `tiles.txt`:
```txt
flags solid water
tile "grass" code:G flags:solid
theme "default"
- variation "grass.png" rules:isolated
- variation:"grass-edge.png" rules:g[2,46]
tile "water" code:W flags:water
theme "default"
- variation "water.png"
```

The `json` transform writes the tiles as `tiles` (with `code`, `layers`, `flags_bitmask` and `themes`) and the flag table as `tile_flags` (each with `index` and `name`), so a consumer can map a bit in `flags_bitmask` back to its flag name. Custom transforms get the same data in `sprat.tiles` and `sprat.tile_flags`.

## Playing a puppet

`spratconvert` only describes puppets; this is what the data means for an engine (or [sprat-lib](https://github.com/pedroac/sprat-lib) user) that draws them:

1. **Rest pose.** Every part shows its first variant, with angle 0, offset 0, scale 1, opacity 1 and a white tint, and every positioned part sits at its `at:`. An animation variant plays on its own clock at its own fps.
2. **Poses.** Apply poses in order, each over the result of the ones before: a pose changes only what it sets. A sequence does the same over time, in whole frames at its fps: frame `k` (from 0) of a step's `in` moves the values its pose sets `(k + 1) / in` of the way from where they are to the pose's, so they arrive on the step's last `in` frame (on its first, with `in:0`); with `ease:step` they stay until then and snap (a part the step lists in `part_eases` eases its own way). Variants switch when the pose arrives, then the step holds for `hold` frames. A cycle plays the steps in order, and for `pingpong` back again without repeating the last (steps 0, 1, 2, 1, 0), each tweening to its pose as usual; a looping sequence starts each cycle where the last ended, and a `once` one holds its last frame.
3. **Placement.** Each part's transform maps the original-image coordinates of the sprite it shows (the space markers and pivots are in, trim included) into the puppet's space:
   - A positioned part's sprite pivot goes at its position; the part is scaled by its `scale_x`, `scale_y` around it, then turns `angle` degrees around it.
   - An attached part's child marker goes where its parent's marker (in the sprite the parent shows), moved by `offset` (in the parent sprite's pixels), lands under the parent's transform; the part is scaled by its own `scale_x`, `scale_y` around that point, then turns by its parent's total turn plus its own `angle`. Only angles add up down the rig: a parent's scale moves its markers and offsets, but doesn't scale its children (so no part is ever skewed).
   - A marker is its `x,y` (a circle's center, a rectangle's corner, a polygon's first vertex). A sprite without a marker named `pivot` uses its pivot for it. A part whose sprite, or whose parent's sprite, lacks the joint's marker can't be placed and isn't drawn, nor is anything hanging from it; neither is a part showing nothing.
   - Place a part's parent before the part itself; a parent may come later in draw order.
4. **Drawing.** Draw the parts in file order with their transforms, each multiplied by its tint and opacity, the puppet's origin (its pivot) at the puppet's place in the world. Constraints, marker shapes and leeways aren't needed to draw: an editor, or an engine's physics step, keeps poses within them.

## In the layout stream

Without `--markers`, `--animations`, `--puppets` or `--tiles`, `spratconvert` reads them from the layout stream itself, so a layout may carry its markers, animations, puppets and tiles. There they share the layout's own `root` line, so leave out theirs: a second `root` line is an error. Layout files may hold `#` comment lines. Tiles and puppets use different keywords (`- variation` and `- sprite`), so a layout may carry both.

Names in these files (sprites, markers, animations, puppets, parts, variants) may not be empty: `""` is reported like a missing name, since the `raw` format writes an empty string as "none".

[Back to README](../../README.md)
