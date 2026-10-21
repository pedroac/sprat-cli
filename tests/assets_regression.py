#!/usr/bin/env python3
"""Asset regression tests: run the sprat tools on tests/assets and compare against text baselines.

Each case stores one baseline file in tests/baselines/assets/<case>.txt with:
  [layout]   exact spratlayout output
  [pack]     per-atlas decoded-pixel hash (or channel means, for resampled output)
  [frames]   exact spratframes output
  [metrics]  atlas size and packing efficiency (informational, shown on mismatch)

Every run also writes the generated files to tests/output/assets/<case>/ (gitignored) for
human inspection: result.txt (same format as the baseline), atlasN.png for packed cases,
frames.png (source sheet with detected frames outlined) for spratframes cases, and
status.txt. tests/output/assets/SUMMARY.txt lists every case's status and timings.

Usage:
  assets_regression.py --spratlayout BIN --spratpack BIN --spratframes BIN [--update] [--bench N]
"""

import argparse
import difflib
import shutil
import hashlib
import io
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageDraw

TESTS_DIR = Path(__file__).resolve().parent
ASSETS_DIR = TESTS_DIR / "assets"
BASELINE_DIR = TESTS_DIR / "baselines" / "assets"
DEFAULT_OUTPUT_DIR = TESTS_DIR / "output" / "assets"  # gitignored; for human inspection

SOF_SPRITES = "Streets of Fight files/Sprites"
SOF_STAGE = "Streets of Fight files/Stage Layers"
CROC = "Crocodile Mascot Running Game/transparent PNG"

# Channel-mean tolerance (0-255 scale) for resampled output, where exact pixels
# may differ across platforms/SIMD paths.
MEAN_TOLERANCE = 0.5

# name -> (kind, input, tool args, pack args or None)
# pack args: None = don't pack; [] = pack with defaults. Packs that pass --output-scale
# are compared by channel means instead of exact hashes.
CASES = {
    "sof-sprites-compact": ("layout", SOF_SPRITES, ["--mode", "compact"], []),
    "sof-sprites-pot": ("layout", SOF_SPRITES, ["--mode", "pot"], []),
    "sof-sprites-fast": ("layout", SOF_SPRITES, ["--mode", "fast"], []),
    "sof-sprites-trim-rotate": (
        "layout", SOF_SPRITES,
        ["--mode", "compact", "--trim-transparent", "--rotate", "--padding", "2"], []),
    "sof-sprites-extrude-half": (
        "layout", SOF_SPRITES,
        ["--mode", "compact", "--trim-transparent", "--extrude", "1", "--padding", "2"],
        ["--output-scale", "0.5", "--scale-filter", "bilinear"]),
    "sof-stage-compact": ("layout", SOF_STAGE, ["--mode", "compact", "--trim-transparent"], []),
    "croc-compact": ("layout", CROC, ["--mode", "compact", "--trim-transparent"], []),
    "croc-multipack": (
        "layout", CROC,
        ["--mode", "compact", "--trim-transparent", "--multipack",
         "--max-width", "1024", "--max-height", "1024"], []),
    "frames-d1c4mut": ("frames", "d1c4mut-6db0241c-6952-42d2-8d49-793f34733d25.png", [], None),
    "frames-da9l9w5": ("frames", "da9l9w5-5db4007e-7b57-417d-964c-cc0837ebb65d.png", [], None),
}

FRAME_RE = re.compile(r"^sprite (\d+),(\d+) (\d+),(\d+)")
SPRITE_RE = re.compile(r'^sprite "(?:[^"\\]|\\.)*" (\d+),(\d+) (\d+),(\d+)')
ATLAS_RE = re.compile(r"^atlas (\d+),(\d+)")


class Runner:
    def __init__(self, bins, tmp_root):
        self.bins = bins
        self.tmp_root = tmp_root
        self.timings = {}

    def run(self, case, tool, args, stdin=None):
        # Fresh temp dir per run: spratlayout keeps its cache there, so every run is cold
        # and independent of earlier runs (and of the user's real cache).
        tmp = tempfile.mkdtemp(dir=self.tmp_root)
        env = dict(os.environ, TMPDIR=tmp, TMP=tmp, TEMP=tmp)
        start = time.perf_counter()
        proc = subprocess.run(
            [self.bins[tool], *args], cwd=ASSETS_DIR, env=env,
            input=stdin, capture_output=True)
        elapsed = time.perf_counter() - start
        self.timings.setdefault(case, {}).setdefault(tool, []).append(elapsed)
        if proc.returncode != 0:
            raise RuntimeError(
                f"{tool} {' '.join(args)} exited {proc.returncode}:\n"
                + proc.stderr.decode(errors="replace"))
        return proc.stdout


def normalize_text(data):
    text = data.decode("utf-8").replace("\r\n", "\n")
    # The root line is the absolute assets path, which differs per checkout. Sprite
    # paths are relative to it and already use '/'.
    return re.sub(r'^root ".*"$', 'root "<assets>"', text, flags=re.M)


def input_list(target):
    """Sorted PNG list for --stdin-list. Packing depends on input order, and directory
    iteration order differs between filesystems, so fix the order here instead of using
    --sort (which also changes how the packer orders sprites)."""
    paths = sorted(p.relative_to(ASSETS_DIR).as_posix() for p in (ASSETS_DIR / target).rglob("*.png"))
    return ("\n".join(paths) + "\n").encode("utf-8")


def layout_metrics(layout):
    """Per-atlas (width, height, efficiency) where efficiency = sprite area / atlas area."""
    atlases = []
    for line in layout.splitlines():
        m = ATLAS_RE.match(line)
        if m:
            atlases.append([int(m.group(1)), int(m.group(2)), 0])
            continue
        m = SPRITE_RE.match(line)
        if m and atlases:
            atlases[-1][2] += int(m.group(3)) * int(m.group(4))
    return [(w, h, used / (w * h) if w and h else 0.0) for w, h, used in atlases]


def describe_atlas(index, png_bytes, tolerant):
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    w, h = img.size
    if tolerant:
        means = []
        for band in img.split():
            hist = band.histogram()
            means.append(sum(v * n for v, n in enumerate(hist)) / (w * h))
        return f"atlas {index} {w}x{h} rgba-mean " + ",".join(f"{m:.3f}" for m in means), img
    digest = hashlib.sha256(img.tobytes()).hexdigest()
    return f"atlas {index} {w}x{h} rgba-sha256 {digest}", img


def frames_overlay(image_path, frames):
    """Source sheet with each detected frame outlined, for eyeballing spratframes output."""
    img = Image.open(image_path).convert("RGBA")
    draw = ImageDraw.Draw(img)
    for line in frames.splitlines():
        m = FRAME_RE.match(line)
        if m:
            x, y, w, h = map(int, m.groups())
            draw.rectangle([x, y, x + w - 1, y + h - 1], outline=(255, 0, 255, 255))
    return img


def write_outputs(case_dir, name, spec, actual, images, status_lines):
    """Replace case_dir with this run's outputs: result.txt (baseline format), images, status."""
    shutil.rmtree(case_dir, ignore_errors=True)
    case_dir.mkdir(parents=True)
    with open(case_dir / "result.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write(format_baseline(name, spec, actual))
    for fname, img in images:
        img.save(case_dir / fname)
    with open(case_dir / "status.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(status_lines) + "\n")


def run_case(runner, name, spec):
    """Returns (sections dict, images list of (filename, Image))."""
    kind, target, args, pack_args = spec
    sections = {}
    images = []
    if kind == "frames":
        frames = normalize_text(runner.run(name, "spratframes", [*args, target]))
        sections["frames"] = frames
        images.append(("frames.png", frames_overlay(ASSETS_DIR / target, frames)))
        return sections, images

    layout_bytes = runner.run(name, "spratlayout", ["--stdin-list", *args], stdin=input_list(target))
    layout = normalize_text(layout_bytes)
    sections["layout"] = layout
    metrics = layout_metrics(layout)
    sections["metrics"] = "".join(
        f"atlas {i} {w}x{h} efficiency {eff:.4f}\n" for i, (w, h, eff) in enumerate(metrics))

    if pack_args is not None:
        tolerant = "--output-scale" in pack_args
        lines = []
        for i in range(len(metrics)):
            extra = ["--atlas-index", str(i)] if len(metrics) > 1 else []
            png = runner.run(name, "spratpack", [*pack_args, *extra], stdin=layout_bytes)
            line, img = describe_atlas(i, png, tolerant)
            lines.append(line + "\n")
            images.append((f"atlas{i}.png", img))
        sections["pack"] = "".join(lines)
    return sections, images


def format_baseline(name, spec, sections):
    kind, target, args, pack_args = spec
    if kind == "layout":
        out = [f"# case: {name}\n",
               f"# sorted PNGs under {target!r} | spratlayout --stdin-list {' '.join(args)}\n"]
    else:
        out = [f"# case: {name}\n", f"# spratframes {' '.join(args)} {target!r}\n"]
    if pack_args is not None:
        out.append(f"# spratpack {' '.join(pack_args)}\n".rstrip() + "\n")
    for key in ("layout", "frames", "pack", "metrics"):
        if key in sections:
            out.append(f"[{key}]\n")
            out.append(sections[key])
    return "".join(out)


def parse_baseline(text):
    sections = {}
    current = None
    for line in text.replace("\r\n", "\n").splitlines(keepends=True):
        m = re.match(r"^\[(\w+)\]\n?$", line)
        if m:
            current = m.group(1)
            sections[current] = ""
        elif current is not None:
            sections[current] += line
        # lines before the first section are comments
    return sections


def pack_matches(expected, actual):
    exp_lines = expected.splitlines()
    act_lines = actual.splitlines()
    if len(exp_lines) != len(act_lines):
        return False
    for e, a in zip(exp_lines, act_lines):
        ep, ap = e.split(), a.split()
        if ep[:3] != ap[:3] or ep[3] != ap[3]:
            return False
        if ep[3] == "rgba-mean":
            ev = [float(v) for v in ep[4].split(",")]
            av = [float(v) for v in ap[4].split(",")]
            if any(abs(x - y) > MEAN_TOLERANCE for x, y in zip(ev, av)):
                return False
        elif ep[4] != ap[4]:
            return False
    return True


def side_by_side(expected, actual):
    lines = [f"  baseline: {line}" for line in expected.splitlines()]
    lines += [f"  actual:   {line}" for line in actual.splitlines()]
    return "\n".join(lines)


def compare(name, expected, actual):
    """Returns a list of human-readable problems (empty if the case matches)."""
    problems = []
    for key in ("layout", "frames"):
        if expected.get(key) != actual.get(key):
            diff = list(difflib.unified_diff(
                (expected.get(key) or "").splitlines(), (actual.get(key) or "").splitlines(),
                "baseline", "actual", lineterm="", n=1))
            shown = "\n".join(diff[:40]) + ("\n..." if len(diff) > 40 else "")
            problems.append(f"[{key}] differs:\n{shown}")
    if "pack" in expected or "pack" in actual:
        if not pack_matches(expected.get("pack", ""), actual.get("pack", "")):
            problems.append("[pack] differs:\n" + side_by_side(expected.get("pack", ""), actual.get("pack", "")))
    if problems and "metrics" in actual:
        problems.append("[metrics]\n" + side_by_side(expected.get("metrics", ""), actual.get("metrics", "")))
    return problems


def format_timings(runner):
    lines = ["Timings (seconds, median of runs; informational only):"]
    for case, tools in runner.timings.items():
        parts = [f"{tool} {statistics.median(ts):.3f}" for tool, ts in tools.items()]
        lines.append(f"  {case:28} " + "  ".join(parts))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--spratlayout", required=True)
    parser.add_argument("--spratpack", required=True)
    parser.add_argument("--spratframes", required=True)
    parser.add_argument("--update", action="store_true", help="rewrite baselines from current output")
    parser.add_argument("--bench", type=int, default=1, metavar="N",
                        help="run each case N times and report median timings")
    parser.add_argument("--output-dir", default=None,
                        help="where each case's generated layout text, atlases and frame overlays "
                             "are written for inspection (default: SPRAT_ASSET_OUTPUT or "
                             "tests/output/assets)")
    parser.add_argument("cases", nargs="*", help="subset of case names to run")
    opts = parser.parse_args()

    bins = {k: str(Path(getattr(opts, k)).resolve()) for k in ("spratlayout", "spratpack", "spratframes")}
    names = opts.cases or list(CASES)
    unknown = [n for n in names if n not in CASES]
    if unknown:
        print(f"Unknown case(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    output_dir = Path(opts.output_dir or os.environ.get("SPRAT_ASSET_OUTPUT") or DEFAULT_OUTPUT_DIR)
    summary = []

    with tempfile.TemporaryDirectory() as tmp:
        runner = Runner(bins, tmp)
        failures = 0
        for name in names:
            spec = CASES[name]
            try:
                for _ in range(max(1, opts.bench)):
                    actual, images = run_case(runner, name, spec)
            except RuntimeError as exc:
                print(f"FAIL {name}: {exc}")
                failures += 1
                shutil.rmtree(output_dir / name, ignore_errors=True)
                (output_dir / name).mkdir(parents=True)
                (output_dir / name / "status.txt").write_text(f"FAIL\n{exc}\n", encoding="utf-8")
                summary.append(f"FAIL {name}")
                continue

            baseline_path = BASELINE_DIR / f"{name}.txt"
            if opts.update:
                BASELINE_DIR.mkdir(parents=True, exist_ok=True)
                content = format_baseline(name, spec, actual)
                with open(baseline_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(content)
                print(f"updated {baseline_path.relative_to(TESTS_DIR.parent)}")
                write_outputs(output_dir / name, name, spec, actual, images, ["updated baseline"])
                summary.append(f"updated {name}")
                continue

            if not baseline_path.exists():
                problems = [f"missing baseline {baseline_path} (run with --update)"]
            else:
                expected = parse_baseline(baseline_path.read_text(encoding="utf-8"))
                problems = compare(name, expected, actual)

            if problems:
                failures += 1
                print(f"FAIL {name}")
                for p in problems:
                    print("  " + p.replace("\n", "\n  "))
                write_outputs(output_dir / name, name, spec, actual, images, ["FAIL", *problems])
                summary.append(f"FAIL {name}")
            else:
                print(f"ok   {name}")
                write_outputs(output_dir / name, name, spec, actual, images, ["ok"])
                summary.append(f"ok   {name}")

    timings = format_timings(runner)
    print("\n" + timings)
    output_dir.mkdir(parents=True, exist_ok=True)
    with open(output_dir / "SUMMARY.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(summary) + "\n\n" + timings + "\n")
    print(f"Generated files: {output_dir.resolve()}")
    if failures:
        print(f"\n{failures} case(s) failed. If the change is intended, update baselines with "
              "scripts/update_asset_baselines.sh.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
