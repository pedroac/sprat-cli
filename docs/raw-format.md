# Raw format

The `raw` transform of `spratconvert` writes the layout's metadata for loading from C.

`raw` writes a `.sprat` file that a C program can read with a few lines of code: one record
per line, every value a space-separated token, and every count written before the records
it counts, so a loader can size each array before filling it. The `sheet` line also carries
totals for the whole file (`MARKERS`, global ones included, `VERTICES`, `FRAMES`, puppet
`PARTS`, `VARIANTS`, `JOINTS`, `POSES`, pose `SETTINGS`, `SEQUENCES` and their `STEPS`, the steps' `PART_EASES`, and `STRING_BYTES`, the UTF-8 bytes of every string), so a loader knows the exact size of the model up front and can allocate it
once; it must reject a file whose records don't match those totals. Strings are length-prefixed (`<UTF-8 byte count>:<bytes>`), so names and
paths may contain spaces; an empty string (`0:`) means "none".

```
sprat-raw 10
sheet SCALE EXTRUDE MULTIPACK ATLASES SPRITES ANIMATIONS PUPPETS GLOBAL_MARKERS MARKERS VERTICES FRAMES PARTS VARIANTS JOINTS POSES SETTINGS SEQUENCES STEPS PART_EASES STRING_BYTES
atlas W H PATH
marker TYPE X Y RADIUS W H VERTICES [VX VY]... NAME
sprite ATLAS X Y W H ROTATION TRIM_L TRIM_T TRIM_R TRIM_B HAS_PIVOT PIVOT_X PIVOT_Y HAS_SLICE SLICE_L SLICE_T SLICE_R SLICE_B SLICE_H SLICE_V ALIAS MARKERS NAME PATH
anim FPS FLIP LOOP ALIAS FRAMES [SPRITE_INDEX]... NAME
puppet PARTS JOINTS POSES SEQUENCES NAME
part PARENT HAS_POSITION X Y VARIANTS NAME
variant KIND INDEX NAME
joint CHILD PARENT CONSTRAINT LEEWAY HAS_ANGLE ANGLE_MIN ANGLE_MAX CHILD_MARKER PARENT_MARKER
pose SETTINGS NAME
set PART VARIANT HAS_ANGLE ANGLE HAS_OFFSET OFFSET_X OFFSET_Y HAS_POSITION X Y HAS_SCALE SCALE_X SCALE_Y HAS_OPACITY OPACITY HAS_TINT TINT_R TINT_G TINT_B
sequence FPS LOOP STEPS NAME
step POSE IN HOLD EASE PART_EASES [PART EASE]...
```

`GLOBAL_MARKERS` `marker` lines follow the atlases: the markers file's markers before any
`path`. Each sprite's `MARKERS` `marker` lines follow it. `HAS_PIVOT` tells a pivot at `0,0`
from no pivot (then `PIVOT_X` and `PIVOT_Y` are 0). A sprite's `ALIAS` is the index of the
regular sprite whose atlas region a deduplicated sprite shares, or `-1`. Enums are numbers: marker types `point`=0,
`circle`=1, `rectangle`=2, `polygon`=3; slice modes `stretch`=0, `repeat`=1, `mirror`=2,
`fixed`=3; `FLIP` bits `h`=1, `v`=2; `LOOP` bits `once`=1, `pingpong`=2 (so `loop`=0,
`once`=1, `pingpong`=2, `once-pingpong`=3; an alias has its source's); flags are `0`/`1`. `ROTATION` is in clockwise degrees
(0, 90 or 270). An alias animation has `ALIAS` set to the index of the animation it replays
and no frames of its own; otherwise `ALIAS` is `-1`. The `PUPPETS` puppets follow the
animations: each `puppet` line is followed by its `PARTS` `part` lines, in draw order, each
followed by its `VARIANTS` `variant` lines, and then by its `JOINTS` `joint` lines. A part's
`PARENT` and a joint's `CHILD` and `PARENT` are part indexes within the puppet (`-1` for no
parent). A part that isn't attached has `HAS_POSITION` 1 and `X Y`, where its sprite's pivot
goes in the puppet's space (atlas pixels, Y down, origin at the puppet's pivot); an attached
part has `HAS_POSITION` 0 and `X Y` 0. A joint with `CONSTRAINT` 0 is its child's parent
joint (one per part with a parent); `CONSTRAINT` 1 marks a further attachment. Each
puppet's `POSES` `pose` lines follow its joints, each followed by its `SETTINGS` `set` lines
in part order: `PART` is a part index, `VARIANT` an index into that part's variants or `-1`
to keep it, and `ANGLE`, `OFFSET_X OFFSET_Y` (atlas pixels), `X Y`, `SCALE_X SCALE_Y` (decimal factors),
`OPACITY` (decimal, 0 to 1) and `TINT_R TINT_G TINT_B` (0 to 255) apply only with their
flags (otherwise angles, offsets and positions are 0, scales and opacity 1 and the tint 255 255 255). Each puppet's `SEQUENCES` `sequence` lines follow its poses, each
followed by its `STEPS` `step` lines in play order: `POSE` is a pose index, `IN` and `HOLD`
frames at `FPS`, and `EASE` `linear`=0 or `step`=1, followed by `PART_EASES` `PART EASE` pairs: the parts (part indexes, set by the pose, in part order) that ease the other way; `LOOP` uses the same bits as an
`anim` line. A variant's `KIND` is `sprite`=0 (`INDEX` is a sprite index) or `animation`=1 (`INDEX`
is an animation index). A joint's markers are names, looked up in the markers of the sprite
each part shows; `LEEWAY` is in atlas pixels, and `ANGLE_MIN` and `ANGLE_MAX` apply only with
`HAS_ANGLE`. Values have the same meaning as in
`json`, and the first line carries the format version (version 2 added `LOOP`; version 3 dropped the normal
and collision map fields; version 4 added `GLOBAL_MARKERS`, their lines and `HAS_PIVOT`, and
replaced the sprite's `ALIAS_OF` path with the `ALIAS` index; version 5 added puppets; version
6 added the puppet's `REFERENCE` and each part's `HAS_POSITION X Y`; version 7 removed
`REFERENCE` and added joint constraints and poses; version 8 added sequences; version 9 added each step's `PART_EASES`; version 10 added each setting's `HAS_SCALE SCALE_X SCALE_Y HAS_OPACITY OPACITY HAS_TINT TINT_R TINT_G TINT_B`).

Exported coordinates and sizes stay within 2^24 (16777216) atlas pixels, the limit
[sprat-lib](https://github.com/pedroac/sprat-lib) enforces, so sums of a few values fit in an
`int` and each converts to `float` exactly. Layouts are rejected when an atlas side, the
extrude, or a sprite's size with its trim restored exceeds it after scaling, and
`spratconvert` rejects markers and puppet leeways whose scaled values do.

[Back to the README](../README.md)
