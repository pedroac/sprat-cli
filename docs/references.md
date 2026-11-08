# References

Free sprites and background reading on texture atlases.

## Free sprite sources

Sample asset source used in this page: https://opengameart.org/content/the-robot-free-sprite

- https://kenney.nl/assets (CC0/public-domain-style game assets)
- https://opengameart.org/ (mixed licenses, check each pack)
- https://itch.io/game-assets/free/tag-sprites (license varies by author)

## Texture optimization

### Shape and Layout
- https://en.wikipedia.org/wiki/Texture_atlas (texture atlas overview)
- https://github.com/juj/RectangleBinPack (MaxRects and related bin-packing approaches)
- https://www.khronos.org/opengl/wiki/Texture (mipmaps, filtering, and texture behavior)

### Color Formats and Precision
- https://www.khronos.org/opengl/wiki/Image_Format (normalized, integer, float, and sRGB formats)
- https://learn.microsoft.com/windows/win32/direct3ddds/dx-graphics-dds-pguide (DDS format/container guidance)

### Compression Formats
- https://www.khronos.org/opengl/wiki/S3_Texture_Compression (S3TC/BC-style compression in OpenGL)
- https://learn.microsoft.com/windows/win32/direct3d11/texture-block-compression-in-direct3d-11 (BC1-BC7 overview and tradeoffs)
- https://github.com/niltonvolpato/python-libsquish (libsquish Python bindings, background on DXT algorithm)
- https://www.khronos.org/opengl/wiki/ASTC_Encode (ASTC encoding reference; similar to DXT but more flexible)

### Sampling Artifacts and Alpha
- https://learnopengl.com/Advanced-OpenGL/Blending (alpha blending behavior)
- https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing (sampling and edge artifacts)
- https://nvidia-developer-blogs.medium.com/alpha-blending-state-of-the-art-b77579460127 (modern alpha blending best practices)

### Asset Deduplication
- https://en.wikipedia.org/wiki/Content-addressable_storage (content hashing and deduplication principles)
- https://isthe.com/chongo/tech/comp/fnv/ (FNV-1a hash function used in sprat-cli)

Platform and engine guidance:

- https://docs.vulkan.org/guide/latest/ (modern cross-platform texture usage guidance)
- https://docs.unity3d.com/Manual/class-TextureImporter.html (Unity import/compression settings)

[Back to the README](../README.md)
