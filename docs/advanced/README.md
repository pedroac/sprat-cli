# Advanced: Technical Deep-Dives

These documents cover **technical specifications**, **format details**, and **implementation notes**.

Use these when you need to:
- Understand file formats in detail
- Write custom transforms or tools
- Debug complex configurations

## Format specifications

- **[Layout Format](layout-format.md)** — Plain-text layout specification
  - Sprite line format, aliases
  - Nine-slice metadata, dithering

- **[Raw Format](raw-format.md)** — C-compatible binary format
  - Serialization details
  - Loading from C/C++

- **[Markers, Animations, Puppets & Tiles](data-files.md)** — Complete DSL reference
  - Marker types and shapes
  - Animation loop modes and aliases
  - Puppet rigging, poses, sequences
  - Playback behavior

## Performance & Optimization

- See [COMPACT_PERFORMANCE.md](../../COMPACT_PERFORMANCE.md) in the repo root
- Benchmark tools in `scripts/benchmark-trim.sh`

## External resources

- [References: Free sprites and texture optimization reading](../references.md)
- [Contributing guidelines](../../CONTRIBUTING.md)

## For tool authors

Writing a tool that reads sprat-cli output?
- See the Raw format for space-efficient, single-pass loading from C
- See the JSON format in the spratconvert Reference for a structured data option
- See custom Jsonnet transforms for format conversion

[Back to README](../../README.md)
