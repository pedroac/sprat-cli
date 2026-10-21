// unity.meta.jsonnet – Unity .meta file spriteSheet section (YAML).
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local id_entry(s) =
  "    - first:\n" +
  "        213: " + s.name_hash_signed + "\n" +
  "      second: " + lib.yaml_string(s.name) + "\n";

// Unity's sprite rect is the trimmed content, so the pivot and the nine-slice border are
// expressed relative to it instead of the original source image.
local rect_pivot(s) = {
  x: if s.content_w > 0 then (s.pivot_x - s.trim_left) / s.content_w else 0,
  y: if s.content_h > 0 then 1 - (s.pivot_y - s.trim_top) / s.content_h else 0,
};

local rect_border(s) =
  if !s.has_slice then { x: 0, y: 0, z: 0, w: 0 }
  else {
    x: std.max(0, s.slice_left - s.trim_left),
    y: std.max(0, s.slice_bottom - s.trim_bottom),
    z: std.max(0, s.slice_right - s.trim_right),
    w: std.max(0, s.slice_top - s.trim_top),
  };

local sprite_rect_entry(s) =
  local pivot = rect_pivot(s);
  local border = rect_border(s);
  "    - serializedVersion: 2\n" +
  "      name: " + lib.yaml_string(s.name) + "\n" +
  "      rect:\n" +
  "        serializedVersion: 2\n" +
  "        x: " + s.x + "\n" +
  "        y: " + s.unity_y + "\n" +
  "        width: " + s.content_w + "\n" +
  "        height: " + s.content_h + "\n" +
  "      alignment: 9\n" +
  "      pivot: {x: " + lib.format_double(pivot.x) + ", y: " + lib.format_double(pivot.y) + "}\n" +
  "      border: {x: " + border.x + ", y: " + border.y + ", z: " + border.z + ", w: " + border.w + "}\n" +
  "      outline: []\n" +
  "      physicsShape: []\n" +
  "      tessellationDetail: 0\n" +
  "      bones: []\n" +
  // spriteID is a 32-hex GUID; Unity itself pads the internal ID's hex the same way.
  "      spriteID: " + s.name_hash_hex + "0000000000000000\n" +
  "      internalID: " + s.name_hash_signed + "\n" +
  "      vertices: []\n" +
  "      indices:\n" +
  "      edges: []\n" +
  "      weights: []\n";

{
  // Unity sprite rects have no rotation field, so rotated sprites would import sideways.
  assert !sprat.has_rotated :
    "Unity .meta sprite sheets do not support rotated sprites; re-run spratlayout without --rotate",
  name: "Unity Meta",
  description: "Unity .meta file spriteSheet section (YAML)",
  extension: ".meta",
  icon: "icons/unity-svgrepo-com.svg",
  content:
    "fileFormatVersion: 2\n" +
    "guid: " + sprat.output_stem_hash_hex + "0000000000000000\n" +
    "TextureImporter:\n" +
    "  internalIDToNameTable:\n" +
    std.join("", [id_entry(s) for s in sprat.sprites]) +
    "  externalObjects: {}\n" +
    "  serializedVersion: 13\n" +
    "  mipmaps:\n" +
    "    mipMapMode: 0\n" +
    "    enableMipMap: 0\n" +
    "    sRGBTexture: 1\n" +
    "    linearTexture: 0\n" +
    "    fadeOut: 0\n" +
    "    borderMipMap: 0\n" +
    "    mipMapsPreserveCoverage: 0\n" +
    "    alphaTestReferenceValue: 0.5\n" +
    "    mipMapFadeDistanceStart: 1\n" +
    "    mipMapFadeDistanceEnd: 3\n" +
    "  bumpmap:\n" +
    "    convertToNormalMap: 0\n" +
    "    externalNormalMap: 0\n" +
    "    heightScale: 0.25\n" +
    "    normalMapFilter: 0\n" +
    "  isReadable: 0\n" +
    "  streamingMipmaps: 0\n" +
    "  streamingMipmapsPriority: 0\n" +
    "  vTOnly: 0\n" +
    "  ignoreMasterTextureLimit: 0\n" +
    "  ignoreMipmapLimit: 0\n" +
    "  isDirectBinding: 0\n" +
    "  importAsync: 0\n" +
    "  filterMode: 0\n" +
    "  aniso: 1\n" +
    "  mipBias: 0\n" +
    "  textureType: 8\n" +
    "  textureShape: 1\n" +
    "  singleChannelComponent: 0\n" +
    "  flipbookRows: 1\n" +
    "  flipbookColumns: 1\n" +
    "  maxTextureSizeSet: 0\n" +
    "  compressionQuality: 50\n" +
    "  textureFormat: -1\n" +
    "  uncompressed: 0\n" +
    "  alphaUsage: 1\n" +
    "  alphaIsTransparency: 1\n" +
    "  spriteMode: 2\n" +
    "  spriteExtrude: 1\n" +
    "  spriteMeshType: 1\n" +
    "  alignment: 0\n" +
    "  spritePivot: {x: 0.5, y: 0.5}\n" +
    "  spritePixelsToUnits: 100\n" +
    "  spriteBorder: {x: 0, y: 0, z: 0, w: 0}\n" +
    "  spriteGenerateFallbackPhysicsShape: 1\n" +
    "  spriteSheet:\n" +
    "    serializedVersion: 2\n" +
    "    sprites:\n" +
    std.join("", [sprite_rect_entry(s) for s in sprat.sprites]) +
    "    outline: []\n" +
    "    physicsShape: []\n" +
    "    bones: []\n" +
    "    spriteID:\n" +
    "    internalID: 0\n" +
    "    vertices: []\n" +
    "    indices:\n" +
    "    edges: []\n" +
    "    weights: []\n" +
    "  spritePackingTag:\n" +
    "  pSDRemoveMatte: 0\n" +
    "  pSDShowRemoveMatteOption: 0\n" +
    "  userData:\n" +
    "  assetBundleName:\n" +
    "  assetBundleVariant:\n",
}
