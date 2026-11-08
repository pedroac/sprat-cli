# Compact packing performance

Notes on the compact-mode search in `spratlayout`, for changes that touch packing speed.
This file describes the current design; measure before and after with
`scripts/benchmark-trim.sh` rather than relying on estimates.

## Current search

`search_compact_layout` (`src/commands/spratlayout/search.cpp`) does two passes:

1. **Seed pass** — tries every sort mode and MaxRects heuristic at a seed width.
2. **Guided width search** — builds a small set of width candidates from the seed,
   the square-root target, the limits and a fixed offset grid, then tries the guided
   sort orders and heuristics at each width.

The guided pass runs on `--threads` workers. Workers claim the next width with an atomic
index (`next_width_index`), so a slow width does not stall a statically assigned range.
Each worker also tries the shelf packer for the same width, so MaxRects and shelf share
one thread lifecycle. A cheap optimistic bound (`width_could_beat`) skips widths that
cannot beat the best layout found so far.

## Remaining costs

- `pack_compact_maxrects` prunes dominated free rectangles with an O(n²) containment
  scan after each placement. This is the main per-packing cost and is single-threaded.
- For each width the guided pass tries four sort orders and three heuristics, plus shelf.
  Early exit after a candidate can no longer beat the best helps, but the first
  combinations are still all attempted.
- The width candidate set is deliberately small (four anchors times the offset grid),
  not an exhaustive scan. Widening it increases quality at a roughly linear cost.

## Measuring

```sh
scripts/benchmark-trim.sh
```

Record wall time, peak memory and the resulting atlas dimensions. A change that improves
packing quality but makes the default path noticeably slower should be exposed as an
option, not made the default.
