# Extracting sprites: spratframes and spratunpack

`spratframes` finds the sprites in an existing sheet, and `spratunpack` writes them out as images.

## Sprite detection (`spratframes`)

`spratframes` scans an image and detects individual sprite boundaries. It can use transparency-based detection (finding connected components of non-transparent pixels) or look for specific rectangle borders.

Pieces of one sprite are folded into a single frame when one piece's box mostly lies inside the other's (at least half of the smaller box); sprites whose boxes merely clip each other, as in tightly packed sheets, stay separate frames, so frames may overlap slightly. Frames are listed in reading order (rows top to bottom, left to right within a row), so animation strips and grids keep their frame order when unpacked as `sprite_0`, `sprite_1`, ... A sheet has a solid background when one opaque color (give or take a little) makes up at least half of the image edge, so sprites touching the edge or its corners don't hide it; only background pixels connected to the image edge are removed, so sprite pixels of the same color are kept unless they touch the background (for example, an outline drawn in the background color).

```sh
./build/spratframes sheet.png > frames.spratframes
```

Options:
- `--has-rectangles`: Use the areas enclosed by lines of the rectangle color as frames instead of using transparency. Works for separate boxes and for grids whose cells share lines, with any line width; each whole cell is a frame, cells must be fully enclosed, and empty cells are skipped.
- `--rectangle-color COLOR`: Border color to detect (e.g., `#FF00FF`, `255,0,255`).
- `--tolerance N`: Manhattan distance for grouping pixels (default: 1). Raise it to 3–5 for effect sheets whose sprites are clouds of separate particles (blood, sparks, smoke), so each effect frame stays whole; keep it at 1 for tightly packed characters, which a higher tolerance can join together. Frames always fit the sprite pixels exactly; the gaps that tolerance bridges are not added to them.
- `--min-size N`: Filter out sprites smaller than NxN pixels.
- `--threads N`: Accepted for compatibility; detection runs on one thread.

Example with magenta borders:
```sh
./build/spratframes --has-rectangles --rectangle-color "#FF00FF" sheet.png > frames.txt
```

## Unpacking atlases (`spratunpack`)

`spratunpack` extracts individual sprites from a texture atlas using a frames definition file.
It can read atlas PNG from a file path, `-`, or stdin (when no atlas path is provided).

**Recommended One-liner:**
```sh
./build/spratframes atlas.png | ./build/spratunpack --output ./extracted
```
When piped from `spratframes`, `spratunpack` can automatically detect the atlas path from the `path ...` line in the stream. That path is as `spratframes` was given it, so relative to the directory it ran in; if it isn't found there, `spratunpack` also looks next to the frames file. `spratframes` exits with an error when it finds no frames, so such a pipeline stops with a clear message.

**Manual usage:**
```sh
./build/spratunpack atlas.png --frames atlas.json --output ./extracted
```

If no output directory is specified, it writes a **TAR archive** to stdout, making it easy to pipe to other tools or network transfers.

```sh
./build/spratunpack atlas.png > sprites.tar
# Equivalent stdin form:
cat atlas.png | ./build/spratunpack --frames atlas.json > sprites.tar
```

Options:
- `-f, --frames PATH`: Path to frames definition (auto-detects `.json` or `.spratframes` only when atlas path is a file).
- `-o, --output DIR`: Destination directory.
- `--keep-trim`: Write only the packed pixels instead of restoring each sprite's original size.
- `--atlas-index N`: For a multipack layout, the atlas the given image is. By default it is the layout's only atlas, or the only one with the image's size; same-size atlases need this option.
- `-j, --threads N`: Number of threads encoding the extracted PNGs (default: all cores). The output is the same with any thread count.

Supported formats:
- TexturePacker/sprat JSON (Hash or Array), including `spratconvert` exports such as `phaser-hash`, `phaser-array`, `unity.json` and `aseprite`
- Phaser multi-atlas JSON (`textures: [...]`): the texture whose `image` matches the atlas file name is unpacked
- `spratlayout` layouts (including multipack layouts, one atlas at a time, and the `alias` lines of deduplicated sprites), and the minimalist `.spratframes` format (`path "<sheet>"`, an optional `background <hex>`, then one `sprite rect:<x>,<y>,<w>,<h>` line per frame)

Rotated sprites are turned back, and trimmed sprites are placed in a transparent canvas of their original size (`sourceSize`/`spriteSourceSize` in JSON, trim offsets in layouts, at the layout's `scale`), so unpacked animation frames stay aligned. Output files are named after the frames, with `.png` added unless the name already ends with it. Every frame is checked before anything is written: frames outside the atlas, frames that don't fit their original size, original sizes beyond 32768 pixels a side or 100 megapixels (usually a corrupt `sourceSize`; `--keep-trim` doesn't restore them), unsafe names or two frames writing the same file make `spratunpack` fail without writing output.

If no frames file is specified, `spratunpack` will look for `<atlas>.json` or `<atlas>.spratframes` automatically only when atlas input is a file path.

[Back to the README](../README.md)
