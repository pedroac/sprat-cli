# Layout Format (Technical)

This is the complete technical specification of the plain-text layout format that `spratlayout` writes and other tools read.

For common use cases and simple examples, see [Markers & Animations guide](../guides/markers-animations.md).

## Format specification

A layout `sprite` line is `sprite "<path>" rect:<x>,<y>,<w>,<h>` followed by optional tokens, in any order and each at most once:

- `"<path>"`: source image path.
- `rect:<x>,<y>,<w>,<h>`: top-left position in the atlas and the size written there (after trimming; for a rotated sprite, the size after rotation). The size must be positive.
- `trim:<left>,<top>,<right>,<bottom>`: pixels trimmed from each side of the original image.
- `rotated`: the sprite was packed rotated 90 degrees in the atlas, clockwise unless the layout has a `rotation ccw` line.
- `dither`, `colors:<N>`, `slice:<L>,<T>,<R>,<B>[,<H>,<V>]`: see below.

A deduplicated sprite is `alias "<original>" -> "<duplicate>"`, with its own `trim:` when it differs. Other layout lines are `root "<dir>"`, `scale <factor>`, `extrude <n>`, `multipack true|false`, `rotation cw|ccw` and `atlas <width>,<height>`; the sprites after an `atlas` line belong to that atlas. Anything else is an error.

## Nine-slice metadata

Annotate sprites with nine-slice insets and per-axis fill modes for UI scaling. Add a `slice:` token to any sprite line in the layout:

```
slice:L,T,R,B[,H_MODE,V_MODE]
```

- `L,T,R,B` — non-negative integer insets (left, top, right, bottom) defining the nine-slice grid,
  in original-image pixels. They must fit the image: `L+R` must not exceed its width, nor `T+B`
  its height (for a nine-patch, the content inside the marker border); layouts that break this
  are rejected. When the layout is scaled, `spratconvert` rounds each inset to atlas pixels and
  trims any rounding excess from the right and bottom insets, so exported insets always fit the sprite.
- `H_MODE,V_MODE` — optional fill modes for the horizontal and vertical stretchable regions:
  - `stretch` (default) — scales the region to fill the available space.
  - `repeat` — tiles the region at its original size without scaling.
  - `mirror` — tiles with alternating flips.
  - `fixed` — the region is not resized; source pixels are used as-is.

Examples:
```
sprite "panel.png" rect:0,0,64,64 slice:8,8,8,8
sprite "border.png" rect:0,0,64,64 slice:8,8,8,8,repeat,stretch
sprite "frame.png" rect:0,0,64,64 slice:10,12,10,12,mirror,mirror
sprite "button.png" rect:0,0,32,32 slice:4,4,4,4,fixed,fixed
```

When `slice:` is present, `spratconvert` emits `slice_left`, `slice_top`, `slice_right`, `slice_bottom`, `slice_h`, and `slice_v` fields per sprite in the transform data.

### Android nine-patch auto-detection
`spratlayout` automatically detects nine-slice insets from Android [nine-patch](https://developer.android.com/studio/write/draw9patch) images (files ending in `.9.png`). The 1px marker border is stripped and the stretch region is read from the opaque black pixels on the top row and left column. No manual `slice:` annotation is needed — the layout file will contain the correct `slice:` token after generation.

Auto-detected insets always use `stretch` for both fill modes. To use `repeat`, `mirror`, or `fixed`, manually edit the emitted `slice:` token in `layout.txt`.

## Per-sprite dithering and quantization

Advanced users can manually edit `layout.txt` to apply per-sprite effects.
- `dither`: Enables ordered dithering for the sprite.
- `colors:N`: Posterizes the sprite to N levels per RGB channel (2-256), i.e. at most N³ colors; alpha is kept.

Example layout line:
`sprite "./frames/robot.png" rect:0,0,128,128 dither colors:16`

[Back to README](../../README.md)
