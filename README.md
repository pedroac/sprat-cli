# sprat-cli

**The UNIX-way sprite sheet generator.**

![sprat-cli screenshot](README-assets/screenshot.png)

`sprat-cli` is a modular toolkit for generating sprite sheets (texture atlases) from the command line. Unlike monolithic GUI tools, it splits the packing process into discrete, pipeable commands. This makes it perfect for:

*   **CI/CD Pipelines**: Automate asset generation in your build process.
*   **Shell Scripting**: Integrate naturally with `|`, `>`, and standard text tools.
*   **Game Development**: Optimized packing algorithms for GPU memory, plus markers, animations and cutout puppets (rigs, poses and pose sequences) for engines.
*   **Web & Apps**: Export to CSS, JSON, XML, or custom formats.

| Tool | What it does |
|---|---|
| `spratlayout` | Scans images and computes their positions in the atlas (a plain-text layout) |
| `spratpack` | Reads a layout and writes the atlas image |
| `spratconvert` | Turns a layout into metadata: Godot, Unity, Phaser, libGDX, Aseprite, plist, JSON, XML, CSV, CSS or your own Jsonnet transform |
| `spratframes` | Detects the sprites in an existing sheet |
| `spratunpack` | Extracts sprites from an atlas |

## Quick start

Build:

```sh
sh build.sh
```

The binaries are generated in the `build/` directory. Make an atlas and its metadata from a folder of images:

```sh
./build/spratlayout ./frames --trim-transparent --padding 2 > layout.txt
./build/spratpack < layout.txt > spritesheet.png
./build/spratconvert --transform json < layout.txt > layout.json
```

Recover the frames of an existing sheet:

```sh
./build/spratframes sheet.png | ./build/spratunpack --output ./recovered_frames
```

Install binaries, man page, and global profile config:

```sh
sudo cmake --install build
```

Every tool has `--help`, and `man ./man/sprat-cli.1` describes them all.

## Workflow

Each tool does one thing well and communicates via text:

```
./frames ──► spratlayout ──► layout.txt ──┬──► spratpack ────► spritesheet.png
                                          └──► spratconvert ─► layout.json

sheet.png ──► spratframes ──► frames.txt ──► spratunpack ──► ./recovered_frames
```

`spratlayout` does math only and does not process image pixels, so it is very fast; its layouts are also cached.

## Documentation

- [spratlayout](docs/spratlayout.md): packing modes (with examples), profiles, options, caching, deduplication, multipacking, rotation
- [spratpack](docs/spratpack.md): Zopfli, dilation, DXT compression, output scale, variant atlases
- [spratconvert](docs/spratconvert.md): built-in formats, pivots, custom Jsonnet transforms
- [Markers, animations and puppets](docs/data-files.md)
- [Layout format](docs/layout-format.md), including nine-slice and dithering
- [Raw format](docs/raw-format.md), for loading metadata from C
- [Extracting sprites](docs/extracting.md): spratframes and spratunpack
- [References](docs/references.md): free sprites and texture optimization reading

## Getting help

- Every tool has `--help`: `./build/spratlayout --help`, `./build/spratpack --help`, etc.
- Full manual: `man ./man/sprat-cli.1` (or `man sprat-cli` after installing)
- Detailed docs: see the links above

## Contributing

Suggestions, pull requests, and forks are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).

## Support

[![Buy Me A Coffee](https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=pedroac&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff)](https://buymeacoffee.com/pedroac)
