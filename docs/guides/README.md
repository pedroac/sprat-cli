# Guides: How to Use sprat-cli

These guides focus on **practical examples** and **common workflows**. Start here.

New to sprite sheets? Read [New to sprite sheets?](../../README.md#new-to-sprite-sheets)
in the README first. It explains atlases, layouts, trimming, padding and metadata in plain
language.

Not sure whether sprat-cli fits your workflow? See
[Who uses it, and why](../../README.md#who-uses-it-and-why).

## By tool

- **[spratlayout](spratlayout.md)** — Generate layouts from your images
  - Packing modes, profiles, common options
  - Typical workflows for different platforms
  
- **[spratpack](spratpack.md)** — Create sprite sheet images
  - Output options, frame visualization
  - Variant atlases (palette swaps)
  
- **[spratconvert](spratconvert.md)** — Generate metadata
  - Supported formats (Godot, Unity, Phaser, etc.)
  - Auto-detection and custom transforms
  
- **[Extracting sprites](extracting.md)** — Recover sprites from existing sheets
  - spratframes & spratunpack
  - Detection algorithms

- **[Integrating sprat-cli](integrating.md)** — Call the tools from a GUI, build script
  or another tool
  - Pipeline contract, exit codes, stdout/stderr
  - Formats, profiles, output paths

- **[Troubleshooting](troubleshooting.md)** — Common errors and fixes

## By topic

- **[Markers & Animations](markers-animations.md)** — Add metadata to sprites
  - Pivot points, hitboxes
  - Animation sequences with timing
  - Aliases and flipping

## Next steps

- **Want all the options?** → See [Reference](../reference/)
- **Need technical details?** → See [Advanced](../advanced/)
- **Not finding what you need?** → Run `./build/{tool} --help`
- **Found a bug or want a feature?** → Open an issue. See
  [CONTRIBUTING.md](../../CONTRIBUTING.md).

[Back to README](../../README.md)
