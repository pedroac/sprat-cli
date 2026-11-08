# Integrating sprat-cli

This guide is for programmers calling `sprat-cli` from a GUI, build script, or another
tool. You do not need to link a library: run the commands and exchange text or files.

## Pipeline at a glance

```text
images ──► spratlayout ──► layout text ──┬──► spratpack ────► atlas image(s)
                                         └──► spratconvert ─► metadata

sheet image ──► spratframes ──► frames text ──► spratunpack ──► sprite images
```

## Calling the tools

- **Results go to stdout.** Warnings and errors go to stderr.
- **Exit code 0** means success. **Exit code 1** means a handled error (bad input, a
  missing file, a layout that does not fit, and so on).
- Read stdout and stderr separately. Do not parse human-readable error messages; show
  them to the user and use the exit code to decide what happened.
- Use absolute paths for input and output. The tools resolve relative paths against the
  process working directory, not against the executable.
- Every tool supports `--help` and `--version`.

Example (Python):

```python
import subprocess

layout = subprocess.run(
    ["spratlayout", "/assets/frames", "--trim-transparent", "--padding", "2"],
    capture_output=True,
    text=True,
)
if layout.returncode != 0:
    raise RuntimeError(layout.stderr)

atlas = subprocess.run(
    ["spratpack"],
    input=layout.stdout.encode(),
    capture_output=True,
    text=False,
)
if atlas.returncode != 0:
    raise RuntimeError(atlas.stderr.decode())

with open("/assets/spritesheet.png", "wb") as f:
    f.write(atlas.stdout)
```

For binary output, capture stdout as bytes (`text=False` in Python). On Windows, the
tools set their own stdin/stdout to binary mode where needed.

## Discovering formats and settings

- `spratconvert --list-transforms-json` prints the available transforms as JSON:
  `name`, `description`, `extension`, `icon`, and `group` for groups.
- `spratconvert --transforms-dir` prints the directory transforms are loaded from.
- `spratlayout --default-profiles-config` prints the profile config path that would be
  used.
- `spratlayout --profiles-config PATH` uses an explicit profile file. A GUI can save its
  settings as a profile and pass this flag.
- `spratlayout --help` lists every option, including the profile keys.

## Input modes

`spratlayout` accepts:

- a folder (subfolders included),
- a list file (one image path per line, plus `root` and `exclude` directives),
- `--stdin-list` (the same list format on stdin),
- `-` (a tar archive on stdin),
- a `.tar`, `.tar.gz`, `.tar.bz2`, `.tar.xz`, `.zip`, `.7z` or `.rar` path.

`spratpack` reads the layout from stdin. `spratconvert` reads the layout from stdin and
writes metadata to stdout, or to files with `--output-dir`. `spratframes` reads an image
path and writes frames text. `spratunpack` reads an atlas path (or `-`/stdin) and a frames
file.

## Output paths

- `spratpack -a PATTERN` writes atlas files instead of stdout. `%d` is replaced by the
  atlas index, e.g. `atlas_%d.png`.
- `spratconvert -a PATTERN` names the atlas files and seeds stable IDs (such as Unity
  GUIDs). `--image-path PATTERN` controls the path written inside the metadata.
- `spratconvert --output-dir DIR` writes multi-file transforms (Unity, plist, Aseprite,
  ...) to a directory. Some formats require it.
- `spratunpack --output DIR` writes sprite files; without it, a TAR stream goes to stdout.

If the metadata and the atlas live in different folders, use `--atlas` for the output
files and `--image-path` for the reference inside the metadata. See the
[spratconvert reference](../reference/spratconvert.md).

## Threads and caching

- `--threads N` controls worker threads where the tool supports it. The output is the
  same with any thread count.
- `spratlayout` caches image measurements and layouts in a private folder under the
  system temp directory. Reusing the same inputs and options is fast.
- `--debug` bypasses the layout cache and adds a comment header to the layout.
- To force a clean run, point `TMPDIR` (Linux/macOS) or `TEMP`/`TMP` (Windows) at a fresh
  folder.

## Error handling

- Show stderr to the user. Warnings are prefixed with `Warning:`; errors are prefixed
  with `Error:`.
- Treat any non-zero exit code as failure. Do not write partial output yourself; the
  tools remove partial files on write failure where possible.
- For a GUI, keep the last command and its stderr so users can report a reproducible
  problem.

## Versioning

Use `--version` to show the tool version. The layout cache is keyed by the version, so a
new release does not reuse old cached layouts.

## Testing an integration

The repository tests the command-line contract with shell integration tests. When adding
a GUI or wrapper, test at least:

- a folder with mixed image sizes,
- trimming and padding,
- a multipack layout,
- your target engine format,
- an error path (missing image or bad layout).

See `tests/` for examples and [CONTRIBUTING.md](../../CONTRIBUTING.md) for how to run the
suite.
