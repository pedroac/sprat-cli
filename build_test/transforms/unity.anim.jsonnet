// unity.anim.jsonnet – Unity AnimationClip (.anim) per-animation files.
local sprat = std.extVar("sprat");
local lib = import "sprat.libsonnet";

local frame_entry(frame, i, fps) =
  "    - time: " + lib.format_double(i / fps) + "\n" +
  "      value: {fileID: " + frame.name_hash_decimal +
  ", guid: " + sprat.output_stem_hash_hex + "0000000000000000, type: 3}\n";

local render_clip(anim) =
  local eff_fps = if anim.fps > 0 then anim.fps else 8;
  "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n" +
  "--- !u!74 &740000" + anim.index + "\n" +
  "AnimationClip:\n" +
  "  m_ObjectHideFlags: 0\n" +
  "  m_CorrespondingSourceObject: {fileID: 0}\n" +
  "  m_PrefabInstance: {fileID: 0}\n" +
  "  m_PrefabAsset: {fileID: 0}\n" +
  "  m_Name: " + anim.name + "\n" +
  "  serializedVersion: 6\n" +
  "  m_Legacy: 0\n" +
  "  m_Compressed: 0\n" +
  "  m_UseHighQualityCurve: 1\n" +
  "  m_RotationCurves: []\n" +
  "  m_CompressedRotationCurves: []\n" +
  "  m_EulerCurves: []\n" +
  "  m_PositionCurves: []\n" +
  "  m_ScaleCurves: []\n" +
  "  m_FloatCurves: []\n" +
  "  m_PPtrCurves:\n" +
  "  - curve:\n" +
  std.join("", [frame_entry(anim.frames[i], i, eff_fps) for i in std.range(0, std.length(anim.frames) - 1)]) +
  "    attribute: m_Sprite\n" +
  "    path:\n" +
  "    classID: 212\n" +
  "    script: {fileID: 0}\n" +
  "  m_AnimationClipSettings:\n" +
  "    serializedVersion: 2\n" +
  "    m_AdditiveReferencePoseClip: {fileID: 0}\n" +
  "    m_AdditiveReferencePoseTime: 0\n" +
  "    m_StartTime: 0\n" +
  "    m_StopTime: " + lib.format_double(anim.duration) + "\n" +
  "    m_OrientationOffsetY: 0\n" +
  "    m_Level: 0\n" +
  "    m_CycleOffset: 0\n" +
  "    m_HasAdditiveReferencePose: 0\n" +
  "    m_LoopTime: 1\n" +
  "    m_LoopBlend: 0\n" +
  "    m_LoopBlendOrientation: 0\n" +
  "    m_LoopBlendPositionY: 0\n" +
  "    m_LoopBlendPositionXZ: 0\n" +
  "    m_KeepOriginalOrientation: 0\n" +
  "    m_KeepOriginalPositionY: 1\n" +
  "    m_KeepOriginalPositionXZ: 0\n" +
  "    m_HeightFromFeet: 0\n" +
  "    m_Mirror: 0\n" +
  "  m_EditorCurves: []\n" +
  "  m_EulerEditorCurves: []\n" +
  "  m_HasGenericRootTransform: 0\n" +
  "  m_HasMotionFloatCurves: 0\n" +
  "  m_Events: []\n";

{
  name: "Unity AnimationClip",
  description: "Unity AnimationClip (.anim) sprite animation; GUIDs match the unity.meta transform output; use --output-dir to write one .anim file per animation",
  extension: ".anim",
  files: [
    { filename: anim.name + ".anim", content: render_clip(anim) }
    for anim in sprat.animations
  ],
}
