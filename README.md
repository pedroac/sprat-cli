# sprat-cli
Command-line sprite sheet generator.

![sprat-cli screenshot](README-assets/screenshot.png)

## Motivation

This project started after using TexturePacker and looking for free, open-source CLI tools to support a sprite sheet generation pipeline.

## Principles

- Keep it simple.
- Do one thing, and do it right.
- Prioritize output-driven workflows.
- Work naturally with `|` and `>`.
- Keep dependencies minimal.
- Automate setup during compilation when possible (for example, downloading dependencies).
- Input flexibility: accept sprite frames of any size.
- Prioritize generated image optimization for GPU usage over packing algorithm runtime.
- Focus on usefulness.
- No GUI: the CLI can later be used to build one.
- Be usable in command lines, CI/CD, and Git-based pipelines.
- Encourage AI-assisted workflows (for example, Codex) for faster iteration and maintenance.
- Stay open source and free.

## Getting started

Build:

```sh
cmake .
make
```

If `stb/` headers are missing and you want CMake to fetch them:

```sh
cmake -DSPRAT_DOWNLOAD_STB=ON -DSTB_REF=master .
make
```

Generate layout first (most common workflow):

```sh
./spritelayout ./frames > layout.txt
```

Inspect layout text:

```sh
head -n 20 layout.txt
```

Pack PNG from that layout:

```sh
./spritepack < layout.txt > spritesheet.png
```

Optional one-pipe run:

```sh
./spritelayout ./frames --trim-transparent --padding 2 | ./spritepack > spritesheet.png
```

Convert layout to JSON/CSV/XML/CSS:

```sh
./spratconvert --transform json < layout.txt > layout.json
```

Manual page:

```sh
man ./man/sprat-cli.1
```

## Build

```sh
cmake .
make
```

This builds three binaries:

- `spritelayout`
- `spritepack`
- `spratconvert`

## Test

Run the end-to-end pipeline test:

```sh
ctest --test-dir tests --output-on-failure
```

This test generates tiny PNG fixtures, runs `spritelayout` to produce layout text,
then runs `spritepack` and verifies the output is a valid PNG.

## Workflow

`spritelayout` scans a folder of input images and prints a text layout to stdout:

```sh
./spritelayout ./frames > layout.txt
```

Profile differences (concise):

- `desktop`: MaxRects + GPU-oriented selection (good default).
- `mobile`: `desktop` behavior with default limits `2048x2048`.
- `space`: MaxRects + tighter area preference.
- `fast`: shelf-style packing + GPU-oriented selection (faster runtime).
- `css`: shelf-style packing + area-oriented selection (stable/simple CSS workflows).
- `legacy`: POT output + default limits `1024x1024`.

`spritelayout` options:

- `--profile desktop|mobile|legacy|space|fast|css` (default: `desktop`)
- `--padding N` (default: `0`)
- `--scale F` (default: `1`, for example `0.5` for half-size output)
- `--trim-transparent` (auto-crop transparent borders)
- `--max-width N` / `--max-height N` (optional atlas limits)

Why these options help:

- `--padding N`: avoids texture bleeding/artifacts from sampling and subpixel math.
- `--scale F`: generate smaller atlases for lower-resolution targets (for example mobile variants).
- `--trim-transparent`: removes empty borders to reduce atlas usage.
- `--max-width/--max-height`: enforce hardware/platform texture limits.
- `spritepack --frame-lines`: visual debug of sprite bounds, spacing, and overlaps.

Example recipes:

```sh
# 1) desktop (default): GPU-oriented packing
./spritelayout ./frames --profile desktop > layout_desktop.txt

# 2) mobile: desktop behavior + default 2048x2048 atlas limits
./spritelayout ./frames --profile mobile > layout_mobile.txt

# 3) space: tighter area packing
./spritelayout ./frames --profile space > layout_space.txt

# 4) fast: quicker shelf-style packing
./spritelayout ./frames --profile fast > layout_fast.txt

# 5) legacy: POT-oriented output + default 1024x1024 limits
./spritelayout ./frames --profile legacy > layout_legacy.txt

# 6) css: shelf-style profile for CSS sprite workflows
./spritelayout ./frames --profile css > layout_css.txt
```

Size/quality recipes:

```sh
# Trim transparent borders before packing
./spritelayout ./frames --profile desktop --trim-transparent > layout_trim.txt

# Add 2px padding between sprites
./spritelayout ./frames --profile desktop --padding 2 > layout_padding.txt

# Hard atlas limits (max-width/max-height)
./spritelayout ./frames --profile desktop --max-width 1024 --max-height 1024 > layout_1024.txt

# Combine trim + padding + explicit limits
./spritelayout ./frames --profile mobile --trim-transparent --padding 2 \
  --max-width 2048 --max-height 2048 > layout_mobile_tuned.txt
```

Rendering recipes with frame lines:

```sh
# Draw sprite outlines on the packed sheet
./spritepack --frame-lines --line-width 1 --line-color 255,0,0 < layout_desktop.txt > spritesheet_lines.png

# End-to-end pipeline: layout + frame lines
./spritelayout ./frames --profile desktop --trim-transparent --padding 2 | \
  ./spritepack --frame-lines --line-width 2 --line-color 0,255,0 > spritesheet_pipeline_lines.png
```

Scale recipe (smaller output for lower resolutions):

```sh
./spritelayout ./frames --profile mobile --scale 0.5 > layout_mobile_half.txt
./spritepack < layout_mobile_half.txt > spritesheet_mobile_half.png
```

The output format is:

- `atlas <width>,<height>`
- `scale <factor>`
- `sprite "<path>" <x>,<y> <w>,<h>`

When `--trim-transparent` is enabled, sprite lines include crop offsets:

- `sprite "<path>" <x>,<y> <w>,<h> <left>,<top> <right>,<bottom>`

Example output from:

```sh
./spritelayout ./frames --trim-transparent > layout.txt
```

```txt
atlas 1631,1963
scale 1
sprite "./tests/png/Run (6).png" 0,0 335,495 109,54 123,7
sprite "./tests/png/RunShoot (6).png" 345,0 373,495 109,54 85,7
sprite "./tests/png/RunShoot (2).png" 728,0 362,492 121,54 84,10
```

## Layout transforms (`spratconvert`)

`spratconvert` reads layout text from stdin and writes transformed output to stdout.
The term `transform` is used because conversion is template-driven and data-oriented.

List built-in transforms:

```sh
./spratconvert --list-transforms
```

Use a built-in transform:

```sh
./spratconvert --transform json < layout.txt > layout.json
./spratconvert --transform csv < layout.txt > layout.csv
./spratconvert --transform xml < layout.txt > layout.xml
./spratconvert --transform css < layout.txt > layout.css
```

Built-in transform files live in `transforms/`:

- `transforms/json.transform`
- `transforms/csv.transform`
- `transforms/xml.transform`
- `transforms/css.transform`

Each transform is section-based:

- `[meta]` for metadata like `name`, `description`, `extension`
- `[header]` printed once before sprites
- `[sprites]` loop template repeated for each sprite (required)
- `[separator]` inserted between sprite entries
- `[footer]` printed once after sprites

Common placeholders:

- `{{atlas_width}}`, `{{atlas_height}}`, `{{scale}}`, `{{sprite_count}}`
- `{{index}}`, `{{path}}`, `{{x}}`, `{{y}}`, `{{w}}`, `{{h}}`
- `{{src_x}}`, `{{src_y}}`, `{{trim_right}}`, `{{trim_bottom}}`, `{{has_trim}}`
- Escaped path variants: `{{path_json}}`, `{{path_csv}}`, `{{path_xml}}`, `{{path_css}}`

Custom transform example:

```ini
[meta]
name=compact-log

[header]
atlas={{atlas_width}}x{{atlas_height}} sprites={{sprite_count}}

[sprites]
{{index}} {{path}} @ {{x}},{{y}} {{w}}x{{h}}

[separator]
;

[footer]

done
```

Run custom transform:

```sh
./spratconvert --transform ./my.transform < layout.txt > layout.custom.txt
```

Column meanings for the `sprite` line in trim mode:

- `"<path>"`: source image path.
- `<x>,<y>`: top-left position in the output atlas where the trimmed sprite is placed.
- `<w>,<h>`: trimmed width and height written into the atlas.
- `<left>,<top>`: pixels trimmed from the left and top of the original image.
- `<right>,<bottom>`: pixels trimmed from the right and bottom of the original image.

`spritepack` reads that layout from stdin and writes the final PNG spritesheet to stdout:

```sh
./spritepack < layout.txt > spritesheet.png
```

Optional frame divider overlay:

- `--frame-lines` (draw sprite rectangle outlines)
- `--line-width N` (default: `1`)
- `--line-color R,G,B[,A]` (default: `255,0,0,255`)

Example:

```sh
./spritepack --frame-lines --line-width 2 --line-color 0,255,0 < layout.txt > spritesheet.png
```

You can also pipe both commands directly:

```sh
./spritelayout ./frames | ./spritepack > spritesheet.png
```

License for third-party art is defined by the asset author; verify terms before redistribution.

## Image Samples

To regenerate these images:

```sh
./scripts/regenerate-readme-assets.sh
```

What the script does:

- Downloads `https://opengameart.org/sites/default/files/RobotFree.zip`
- Extracts PNG frames into `README-assets/frames`
- Reduces frame sizes with ImageMagick (`FRAME_MAX_SIZE`, default `64x64>`)
- Generates spritesheets from those reduced frames (without resizing output sheets)
- Generates one image per command in `Example recipes` (`recipe-01-...png` to `recipe-12-...png`)

Sample asset source used in this page: https://opengameart.org/content/the-robot-free-sprite

Recipe 1: Desktop (`./spritelayout ./frames --profile desktop > layout_desktop.txt`)

![Recipe 1 Desktop](README-assets/recipe-01-desktop.png)

Recipe 2: Mobile (`./spritelayout ./frames --profile mobile > layout_mobile.txt`)

![Recipe 2 Mobile](README-assets/recipe-02-mobile.png)

Recipe 3: Space (`./spritelayout ./frames --profile space > layout_space.txt`)

![Recipe 3 Space](README-assets/recipe-03-space.png)

Recipe 4: Fast (`./spritelayout ./frames --profile fast > layout_fast.txt`)

![Recipe 4 Fast](README-assets/recipe-04-fast.png)

Recipe 5: Legacy (`./spritelayout ./frames --profile legacy > layout_legacy.txt`)

![Recipe 5 Legacy](README-assets/recipe-05-legacy.png)

Recipe 6: CSS (`./spritelayout ./frames --profile css > layout_css.txt`)

![Recipe 6 CSS](README-assets/recipe-06-css.png)

Recipe 7: Trim (`./spritelayout ./frames --profile desktop --trim-transparent > layout_trim.txt`)

![Recipe 7 Trim](README-assets/recipe-07-trim-transparent.png)

Recipe 8: Padding (`./spritelayout ./frames --profile desktop --padding 2 > layout_padding.txt`)

![Recipe 8 Padding](README-assets/recipe-08-padding-2.png)

Recipe 9: Max 1024 (`./spritelayout ./frames --profile desktop --max-width 1024 --max-height 1024 > layout_1024.txt`)

![Recipe 9 Max 1024](README-assets/recipe-09-max-1024.png)

Recipe 10: Mobile Tuned (`./spritelayout ./frames --profile mobile --trim-transparent --padding 2 --max-width 2048 --max-height 2048 > layout_mobile_tuned.txt`)

![Recipe 10 Mobile Tuned](README-assets/recipe-10-mobile-tuned.png)

Recipe 11: Frame Lines (`./spritepack --frame-lines --line-width 1 --line-color 255,0,0 < layout_desktop.txt > spritesheet_lines.png`)

![Recipe 11 Frame Lines](README-assets/recipe-11-frame-lines-red.png)

Recipe 12: Pipeline Lines (`./spritelayout ./frames --profile desktop --trim-transparent --padding 2 | ./spritepack --frame-lines --line-width 2 --line-color 0,255,0 > spritesheet_pipeline_lines.png`)

![Recipe 12 Pipeline Lines](README-assets/recipe-12-pipeline-lines-green.png)

## Free Sprite Sources

- https://kenney.nl/assets (CC0/public-domain-style game assets)
- https://opengameart.org/ (mixed licenses, check each pack)
- https://itch.io/game-assets/free/tag-sprites (license varies by author)

## Texture Optimization References

Shape and layout:

- https://en.wikipedia.org/wiki/Texture_atlas (texture atlas overview)
- https://github.com/juj/RectangleBinPack (MaxRects and related bin-packing approaches)
- https://www.khronos.org/opengl/wiki/Texture (mipmaps, filtering, and texture behavior)

Color formats and precision:

- https://www.khronos.org/opengl/wiki/Image_Format (normalized, integer, float, and sRGB formats)
- https://learn.microsoft.com/windows/win32/direct3ddds/dx-graphics-dds-pguide (DDS format/container guidance)

Compression formats:

- https://www.khronos.org/opengl/wiki/S3_Texture_Compression (S3TC/BC-style compression in OpenGL)
- https://learn.microsoft.com/windows/win32/direct3d11/texture-block-compression-in-direct3d-11 (BC1-BC7 overview and tradeoffs)

Sampling artifacts and alpha:

- https://learnopengl.com/Advanced-OpenGL/Blending (alpha blending behavior)
- https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing (sampling and edge artifacts)

Platform and engine guidance:

- https://docs.vulkan.org/guide/latest/ (modern cross-platform texture usage guidance)
- https://docs.unity3d.com/Manual/class-TextureImporter.html (Unity import/compression settings)

## Contributing

Suggestions, pull requests, and forks are welcome.

High-impact contribution areas:

- Packaging and distribution:
  - Linux packages (deb/rpm), Homebrew formulae, Scoop/Chocolatey, Arch/AUR, Nix, etc.
  - Release automation and artifact publication for multiple platforms.
- GUI frontends:
  - Desktop/web/mobile wrappers around the CLI pipeline.
  - Workflow-focused tools that call `spritelayout`, `spritepack`, and `spratconvert` under the hood.
- Engine/runtime integrations:
  - Importers/exporters and transform templates for specific game engines or frameworks.
  - Community-maintained presets and examples.
- CI/CD and developer tooling:
  - Cross-platform build/test matrices.
  - Reproducible packaging and versioned release pipelines.

Core scope remains a free UNIX-style CLI. GUI and platform integrations are encouraged as companion projects or optional layers.

## License

MIT. See [LICENSE](LICENSE).

## Support

[![Buy Me A Coffee](https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=pedroac&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff)](https://buymeacoffee.com/pedroac)
