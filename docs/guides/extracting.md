# Extracting sprites: spratframes and spratunpack

`spratframes` finds the sprites in an existing sheet, and `spratunpack` writes them out as images.

## Sprite detection (`spratframes`)

`spratframes` scans an image and detects individual sprite boundaries. It can use transparency-based detection (connected components of non-transparent pixels) or look for rectangle borders.

```sh
./build/spratframes sheet.png > frames.spratframes
```

Options:

- `--has-rectangles`: use the areas enclosed by lines of the rectangle color as frames instead of transparency. Works for separate boxes and for grids whose cells share lines, with any line width; each whole cell is a frame, cells must be fully enclosed, and empty cells are skipped.
- `--rectangle-color COLOR`: border color to detect (for example `#FF00FF` or `255,0,255`).
- `--tolerance N`: Manhattan distance for grouping pixels (default: 1). Raise it to 3-5 for effect sheets whose sprites are clouds of separate particles; keep it at 1 for tightly packed characters.
- `--min-size N`: filter out sprites smaller than NxN pixels (default: 4).
- `--max-sprites N`: maximum number of frames to extract (default: 10000).
- `--threads N`: accepted for compatibility; detection runs on one thread.

Example with magenta borders:

```sh
./build/spratframes --has-rectangles --rectangle-color "#FF00FF" sheet.png > frames.txt
```

`spratframes` exits with an error when it finds no frames, so a pipeline into `spratunpack` stops with a clear message.

## Unpacking atlases (`spratunpack`)

`spratunpack` extracts individual sprites from a texture atlas using a frames definition file. It reads the atlas from a path, `-`, or stdin when no atlas path is given.

Recommended one-liner:

```sh
./build/spratframes atlas.png | ./build/spratunpack --output ./extracted
```

When piped from `spratframes`, `spratunpack` detects the atlas path from the `path ...` line in the stream. That path is relative to the directory where `spratframes` ran; if it is not found there, `spratunpack` also looks next to the frames file.

Manual usage:

```sh
./build/spratunpack atlas.png --frames atlas.json --output ./extracted
```

If no output directory is specified, it writes a TAR archive to stdout:

```sh
./build/spratunpack atlas.png > sprites.tar
cat atlas.png | ./build/spratunpack --frames atlas.json > sprites.tar
```

Options:

- `-f, --frames PATH`: frames definition file (`.json` or `.spratframes`); `-` reads it from stdin.
- `-o, --output DIR`: destination directory.
- `--keep-trim`: write only the packed pixels instead of restoring each sprite's original size.
- `--atlas-index N`: for a multipack layout, the atlas the given image is. By default it is the layout's only atlas, or the only one with the image's size; same-size atlases need this option.
- `-j, --threads N`: number of threads encoding the extracted PNGs (default: all cores). The output is the same with any thread count.
- `--debug`: print the atlas size and each frame's rectangle, canvas and trim offsets to stderr.

Supported formats:

- TexturePacker/sprat JSON (Hash or Array), including `spratconvert` exports such as `phaser-hash`, `phaser-array`, `unity.json` and `aseprite`.
- Phaser multi-atlas JSON (`textures: [...]`): the texture whose `image` matches the atlas file name is unpacked.
- `spratlayout` layouts (including multipack layouts, one atlas at a time, and the `alias` lines of deduplicated sprites), and the minimalist `.spratframes` format (`path "<sheet>"`, an optional `background <hex>`, then one `sprite rect:<x>,<y>,<w>,<h>` line per frame).

Rotated sprites are turned back, and trimmed sprites are placed in a transparent canvas of their original size (`sourceSize`/`spriteSourceSize` in JSON, trim offsets in layouts, at the layout's `scale`), so unpacked animation frames stay aligned. Output files are named after the frames, with `.png` added unless the name already ends with it. Frame names may contain `/` to create subdirectories. Every frame is checked before anything is written: frames outside the atlas, frames that do not fit their original size, original sizes beyond 32768 pixels a side or 100 megapixels, unsafe names (absolute paths, `..`, drive letters, Windows device names) or two frames writing the same file make `spratunpack` fail without writing output.

[Back to the README](../../README.md)
