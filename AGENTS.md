# AGENTS.md

Guidance for AI agents (and humans) working on **sprat-cli**.

## Purpose

sprat-cli is a UNIX-style toolkit for **video game sprite sheets**:

- **Generate** sprite sheets (texture atlases) together with their metadata.
- **Extract** sprites from existing sprite sheets.
- **Convert** metadata to popular engines/frameworks and formats
  (Godot, Unity, Phaser, libGDX, Aseprite, plist, JSON, XML, CSV, ...).

CSS sprites are supported and useful, but they are secondary. When a design
decision trades off between web/CSS and game-engine use, favor game engines.

## Priorities

1. **Correct, high-quality output files.** The generated atlases and metadata
   are the product. Never trade output correctness for speed.
2. **Fast defaults.** The tools are also driven by GUIs, so the default path must
   be good for most cases *and* very fast.
3. **Expensive work is opt-in.** If something needs several seconds to give the
   best result (exhaustive search, heavy compression such as Zopfli, extra
   optimization passes, ...), expose it as an option — never make it the default.

## Tools (pipeline)

| Tool           | Role                                                          |
|----------------|---------------------------------------------------------------|
| `spratlayout`  | Scan images and compute positions (math only, must stay fast) |
| `spratpack`    | Read a layout, load images, write the atlas image             |
| `spratconvert` | Transform a layout into engine/format metadata (Jsonnet)      |
| `spratframes`  | Detect sprite boundaries in an existing sheet                 |
| `spratunpack`  | Extract individual sprites from an atlas                      |

Keep tools composable: plain text layouts on stdin/stdout, no hidden coupling
between commands.

## Code layout

- `src/<tool>.cpp` — thin executable entry points.
- `src/commands/<tool>_command.cpp` — command implementations.
- `src/core/` — shared code (CLI parsing, i18n, layout parsing, output patterns, stb).
- `transforms/*.jsonnet` — output format templates; shared helpers in
  `transforms/sprat.libsonnet`.
- `tests/` — C++ unit tests and shell/Python integration tests, registered in
  `tests/CMakeLists.txt`; asset baselines in `tests/baselines/`.
- `man/sprat-cli.1` — manual page.

## Coding practices

- C++20, CMake. Follow the style of the surrounding code.
- Apply **DRY** when it makes sense: shared logic used by several commands goes
  into `src/core/`; shared transform logic goes into `sprat.libsonnet`.
  Don't force abstractions on code that only looks similar.
- **Isolate hard-to-read code** (bit tricks, packing heuristics, SIMD, format
  quirks) in small, well-named functions or files with a short comment
  explaining *why*, so the rest of the code stays readable.
- Validate input and guard against overflow (image sizes, scales, layout bounds).
- Keep user-facing strings translatable (i18n).
- Must build and pass tests on Linux, macOS and Windows (see `.github/workflows/ci.yml`).

## Build and test

```sh
cmake -S . -B build -DSPRAT_DOWNLOAD_STB=ON -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- Without `-DBUILD_TESTING=ON`, `ctest` finds no tests.
- Add or update tests for every behavior change or bug fix.
- If output intentionally changes, regenerate baselines with
  `scripts/update_asset_baselines.sh` and review `git diff tests/baselines`.
- For performance-sensitive changes, measure before and after
  (see `COMPACT_PERFORMANCE.md`, `scripts/benchmark-trim.sh`).

## Documentation

Any change that affects requirements, usage, options, defaults or results
**must** update the relevant docs in the same change:

- `README.md`
- `man/sprat-cli.1`
- `--help` text of the affected tool
- `README-assets/` examples when visual output changes
  (`scripts/regenerate-readme-assets.sh`)

## Versioning and commits

- **Update `VERSION` before committing** (format: `vMAJOR.MINOR.PATCH`), and
  include it in the same commit as the change.
- Commit messages: `feat: ...`, `fix: ...`, `docs: ...`, `test: ...`, `refactor: ...`.
- Never commit build output (`build*/`, binaries, `_deps/`, generated test output).
