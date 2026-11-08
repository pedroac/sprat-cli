# sprat-cli

**The UNIX-way sprite sheet generator.**

A free, scriptable, high-quality sprite packer for any pipeline.

![sprat-cli screenshot](README-assets/screenshot.png)

`sprat-cli` is a modular toolkit for generating sprite sheets (texture atlases) from the command line. Unlike monolithic GUI tools, it splits the packing process into discrete, pipeable commands. This makes it perfect for:

*   **CI/CD Pipelines**: Automate asset generation in your build process.
*   **Shell Scripting**: Integrate naturally with `|`, `>`, and standard text tools.
*   **Game Development**: Optimized packing algorithms for GPU memory, plus markers, animations and cutout puppets (rigs, poses and pose sequences) for engines.
*   **Web & Apps**: Export to CSS, JSON, XML, or custom formats.

## Who it's for

`sprat-cli` is a free, scriptable, high-quality sprite packer for any pipeline. It is
designed for anyone who wants repeatable atlas builds without a GUI, from a solo
developer to a studio pipeline team.

You do **not** need to be a programmer for the basic workflow: install the tools, put
your images in a folder, and run a few commands.

### Who uses it, and why

| You are... | What you get |
|---|---|
| **A solo indie or small team** | A free MIT-licensed packer with a short command-line workflow, engine-ready metadata, and no per-seat licence. |
| **A technical artist or pipeline engineer** | Deterministic layouts, caching, multipack, deduplication, variant atlases, custom Jsonnet transforms, and headless CI-friendly commands. |
| **A professional studio or big team** | A headless, deterministic packer that fits existing build systems, scales to large sprite sets and variants, and avoids per-seat licensing. |
| **A tool, GUI or plugin developer** | A plain-text pipeline with stable exit codes, JSON transform discovery, a raw C-friendly format, and a layout format you can inspect or generate. |
| **A student, hobbyist or game-jam team** | A small set of focused tools you can learn one at a time, with copy-paste examples. |
| **An open-source or Linux-first project** | An MIT-licensed, build-from-source tool that fits a scripted, reproducible asset pipeline. |

If you want a drag-and-drop editor with a visual packing preview, `sprat-cli` is not that
by itself; it is designed to be the packing engine behind such a tool as well as a CLI
you can use directly.

## New to sprite sheets?

A **sprite sheet** (also called a **texture atlas**) is one image that contains many
smaller images. Games load one texture instead of dozens, which is faster.

The tools split the job into small steps:

1. `spratlayout` looks at your images and decides where each one goes.
2. `spratpack` draws the final sheet image.
3. `spratconvert` writes the metadata that tells your engine where each sprite is.

Useful terms:

| Term | Plain meaning |
|---|---|
| **Atlas** | The final image containing all sprites. |
| **Layout** | A text file listing each sprite's position in the atlas. |
| **Metadata** | A file (JSON, plist, `.tres`, ...) that tells your engine where each sprite is. |
| **Trim** | Remove transparent borders around a sprite to save space. |
| **Padding** | Empty pixels between sprites so they do not bleed into each other. |
| **Extrude** | Repeat a sprite's edge pixels into the padding to avoid seams. |
| **Multipack** | Split sprites across several atlas images when one is too small. |

| Tool | What it does |
|---|---|
| `spratlayout` | Scans images and computes their positions in the atlas (a plain-text layout) |
| `spratpack` | Reads a layout and writes the atlas image |
| `spratconvert` | Turns a layout into metadata: Godot, Unity, Phaser, libGDX, Aseprite, plist, JSON, XML, CSV, CSS or your own Jsonnet transform |
| `spratframes` | Detects the sprites in an existing sheet |
| `spratunpack` | Extracts sprites from an atlas |

## Quick start

### 1. Install

**Option A — download a release.** Check the
[Releases page](https://github.com/pedroac/sprat-cli/releases) for a prebuilt archive
for Linux, macOS or Windows. Unpack it and run the tools from that folder. The release
archives include the built-in transforms and the default profile config.

**Option B — build from source.** You need CMake 3.15 or newer, a C++20 compiler, and
git. Then run:

```sh
sh build.sh
```

The binaries are generated in the `build/` directory. On Windows, use
`build\spratlayout.exe`, `build\spratpack.exe`, and so on.

### 2. Put your images in a folder

Use PNG, JPG, BMP, TGA, GIF, PSD, PNM, HDR or WebP files. Subfolders are included.
Images are read in natural filename order, so `walk2.png` comes before `walk10.png`.

### 3. Make your first atlas

From the project folder, with your images in `./frames`:

```sh
./build/spratlayout ./frames --trim-transparent --padding 2 > layout.txt
./build/spratpack < layout.txt > spritesheet.png
./build/spratconvert --transform json < layout.txt > layout.json
```

You now have:

- `spritesheet.png` — the atlas image.
- `layout.txt` — the plain-text layout (useful for debugging and for re-packing).
- `layout.json` — generic metadata.

### 4. Pick your engine format

Replace `json` with the format your engine expects:

```sh
# Godot
./build/spratconvert --transform godot < layout.txt > sprites.json

# Unity (writes a .json, a .meta and one .anim per animation)
./build/spratconvert --transform unity --output-dir ./out -a atlas.png < layout.txt

# Phaser
./build/spratconvert --transform phaser-hash < layout.txt > sprites.json
```

See the [spratconvert guide](docs/guides/spratconvert.md) for all formats.

### 5. Recover frames from an existing sheet

```sh
./build/spratframes sheet.png | ./build/spratunpack --output ./recovered_frames
```

### Install system-wide (optional)

```sh
sudo cmake --install build
```

Every tool has `--help`, and `man ./man/sprat-cli.1` describes them all.

## If something goes wrong

- **`command not found`** — run the tools with their path (`./build/spratlayout`) or
  install them. On Windows, use `build\spratlayout.exe`.
- **`no valid images found`** — check the folder path and the file extensions. Hidden
  files and `.spratlayoutignore` entries are skipped.
- **`sprites ... exceed the atlas limit`** — raise `--max-width`/`--max-height`, or add
  `--multipack` to split across several atlases.
- **`Failed to load image`** — the path in the layout is wrong. Run the tools from the
  folder that contains your images, or use a list file with explicit paths.
- **The engine shows the wrong frames** — pass `-a` and `--image-path` so the metadata
  points at the atlas the way your engine resolves paths. See
  [Image paths inside the metadata](docs/reference/spratconvert.md).
- **Paths with spaces** — quote them: `"./my frames"`.

More help: [Troubleshooting guide](docs/guides/troubleshooting.md).

## Workflow

Each tool does one thing well and communicates via text:

```
./frames ──► spratlayout ──► layout.txt ──┬──► spratpack ────► spritesheet.png
                                          └──► spratconvert ─► layout.json

sheet.png ──► spratframes ──► frames.txt ──► spratunpack ──► ./recovered_frames
```

`spratlayout` computes positions without writing pixels; with the default options it reads
only image headers, and trimming or deduplication loads pixels when needed. Its layouts are
also cached.

## For GUI and tool developers

The tools are designed to be called from another program:

- Results go to **stdout**; warnings and errors go to **stderr**.
- Exit code **0** means success, **1** means a handled error.
- Layouts and metadata are plain text, so they are easy to inspect and log.
- `--list-transforms-json` lists the available metadata formats as JSON.
- `--transforms-dir` prints the directory where transforms are loaded from.
- `--profiles-config` and `--default-profiles-config` control profile discovery.
- `--stdin-list` reads image paths from stdin; `-` reads a tar archive from stdin.
- `--output-dir` writes multi-file transforms (Unity, plist, Aseprite, ...) to a folder.

See [Integrating sprat-cli](docs/guides/integrating.md) for the pipeline contract and
integration tips.

## Documentation

**Start with the guides** for practical examples and common workflows:
- **[Guides](docs/guides/)** — How to use each tool
  - [spratlayout](docs/guides/spratlayout.md): packing modes, profiles, workflows
  - [spratpack](docs/guides/spratpack.md): basic options, debugging, variants
  - [spratconvert](docs/guides/spratconvert.md): engine formats, auto-detection
  - [Extracting sprites](docs/guides/extracting.md): spratframes and spratunpack
  - [Markers & Animations](docs/guides/markers-animations.md): pivots, hitboxes, sequences
  - [Troubleshooting](docs/guides/troubleshooting.md): common errors and fixes
  - [Integrating sprat-cli](docs/guides/integrating.md): call the tools from a GUI or script

**Need all the options?** See the [Reference](docs/reference/):
- [spratlayout reference](docs/reference/spratlayout.md): all options, algorithms, caching
- [spratpack reference](docs/reference/spratpack.md): compression, GPU formats, scaling
- [spratconvert reference](docs/reference/spratconvert.md): formats, custom transforms, schema

**Technical details** in [Advanced](docs/advanced/):
- [Layout format](docs/advanced/layout-format.md): nine-slice, dithering, format spec
- [Raw format](docs/advanced/raw-format.md): C/C++ loading
- [Data files](docs/advanced/data-files.md): complete marker/animation/puppet DSL
- [References](docs/references.md): free sprites and optimization reading

## Getting help

- Every tool has `--help`: `./build/spratlayout --help`, `./build/spratpack --help`, etc.
- Full manual: `man ./man/sprat-cli.1` (or `man sprat-cli` after installing)
- Detailed docs: see the links above

## Contributing

This is a community project. The repository owner started it and reviews requests, but it
is meant to be built together. Bug fixes, tests, documentation and small improvements are
usually accepted. New features should be discussed in an issue first. Forks are welcome.

Ways to help: report bugs, test new builds, improve the docs, review pull requests, add
engine transforms or presets, and package the tools for your platform. See
[CONTRIBUTING.md](CONTRIBUTING.md).

### AI assistance

AI is used in this project. Documentation is reviewed and updated with AI agents, and pull
requests may be reviewed by agents. A human maintainer makes the final decision.

## License

MIT. See [LICENSE](LICENSE).

## Support

[![Buy Me A Coffee](https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=pedroac&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff)](https://buymeacoffee.com/pedroac)
