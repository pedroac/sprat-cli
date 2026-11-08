# Troubleshooting

Common problems and what to try first.

## `command not found`

Use the path to the binary:

```sh
./build/spratlayout --help
```

On Windows, use `build\spratlayout.exe`. If you installed the tools, make sure the install
directory is on your `PATH`.

## `no valid images found`

- Check the folder path. Run the command from the folder that contains your images, or
  pass the full path.
- Check the file extensions. Supported images are PNG, JPG/JPEG, BMP, TGA, GIF, PSD, PIC,
  PNM/PGM/PPM, HDR and WebP.
- Check `.spratlayoutignore`. It can exclude files and folders.
- If you use a list file, make sure each line names an existing file.

## `Failed to load image`

The path stored in the layout does not point at the image. This often happens when the
layout was generated in one folder and packed in another.

- Run both commands from the same folder.
- Or generate the layout with a `root` line that points at the images.
- Or use a list file with full paths.

## `sprites ... exceed the atlas limit`

The sprites do not fit in one atlas with the current limits.

- Raise `--max-width` and `--max-height`.
- Add `--multipack` to split across several atlases.
- Use `--trim-transparent` to remove empty borders.
- Lower `--padding` if it is larger than needed.

## My engine shows the wrong frames

The metadata points at the atlas incorrectly.

- Pass `-a` with the atlas file name you actually use.
- If the metadata and images are in different folders, pass `--image-path` with the path
  the engine should resolve.
- Use `--output-dir` for formats that write several files (Unity, plist, Aseprite, ...).
- Check the engine guide for your format in the
  [spratconvert guide](spratconvert.md).

## Sprites have seams or dark edges

- Add `--padding 2` (or more) when generating the layout.
- Add `--extrude 1` to repeat edge pixels into the padding.
- In `spratpack`, try `--dilate 1` to bleed colour into transparent pixels.
- Make sure your engine uses the same filtering settings the atlas was prepared for.

## The atlas is larger than expected

- `--mode compact` usually packs tighter than `--mode fast`.
- `--optimize space` minimizes area; `--optimize gpu` prefers GPU-friendly dimensions.
- `--trim-transparent` removes empty borders.
- `--deduplicate exact` merges identical sprites.
- `--rotate` allows 90-degree rotation for a tighter fit.

## `spratframes` finds no frames

- The sheet may have no transparency and no rectangle borders. Try
  `--has-rectangles --rectangle-color "#FF00FF"` if the sheet uses border lines.
- Lower `--min-size` if the sprites are very small.
- Raise `--tolerance` for effect sheets made of separate particles.

## `spratunpack` refuses a frame name

Frame names become file paths. Names that are absolute, contain `..`, use a drive letter
(`C:`), or are Windows device names (`CON`, `NUL`, ...) are refused. Rename the frame in
the frames file and try again.

## `spratunpack` cannot choose an atlas

For a multipack layout, pass `--atlas-index N`. By default it uses the layout's only
atlas, or the only atlas with the same size as the image.

## Paths with spaces or special characters

Quote the path:

```sh
./build/spratlayout "./my frames" --padding 2 > layout.txt
```

In list files, put each path on its own line. `root` and `exclude` accept quoted paths.

## The build is slow or runs out of memory

- Use `--mode fast` (the default) for the quickest layout.
- Limit threads with `--threads 4`.
- For a source build, give `cmake --build` a job count: `cmake --build build --parallel 4`.

## Getting help

- Run `<tool> --help` for the options.
- Read the [guides](README.md) and [reference](../reference/README.md).
- Open an issue with the command, the expected result, the actual output, your OS, and
  the version from `<tool> --version`.
