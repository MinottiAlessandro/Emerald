# Smooth-scrolling prototype

This branch tests browser-like scrolling without adding a dependency. The editor
uses Qt's `QTextEdit` document layout so its vertical scrollbar is measured in
pixels rather than visual lines.

## Behaviour

- Conventional mouse-wheel notches accumulate into a pending pixel target and
  ease to it with an `OutCubic` curve over 140 ms.
- The distance of a notch respects the platform's configured wheel-line count
  and the active editor font's line height.
- High-resolution trackpad `pixelDelta` events are applied directly. The OS is
  already providing fine movement and momentum, so Emerald does not layer a
  second animation over them.
- Read Mode's plain Up/Down keys ease by one font line over 105 ms without moving
  the hidden text cursor.
- Clicking, dragging the scrollbar, resizing, or returning to editing cancels a
  pending animation immediately.

The migration preserves live preview, list hanging indents, custom painting,
folds, overscroll, Quick Jump, source-only undo history, and selection behaviour.

## Release-build benchmark

Measurements used the existing `emerald_perf_tests` executable. Values are the
median of five independent runs for the standard mixed workload (`2500` notes,
`220` words per generated note, and a `2000`-word editor document).

| Metric | `main` | Prototype | Difference |
|---|---:|---:|---:|
| Load editor document | 1.866 ms | 8.614 ms | +6.748 ms |
| Render one viewport | 5.064 ms | 9.057 ms | +3.993 ms |
| RSS after editor | 80,044 KiB | 80,012 KiB | within run noise |

A separate three-run median with an approximately 80,000-word editor document
showed 46.779 ms vs 49.409 ms to load, 7.876 ms vs 27.811 ms to render the
viewport, and 65,156 KiB vs 67,524 KiB RSS. This is the important prototype
tradeoff: the interaction becomes genuinely pixel-smooth with negligible memory
change for ordinary notes, while viewport rendering costs more—especially for a
very large single note.

## Evaluation

When testing interactively, focus on whether rapid wheel input feels continuous,
whether the 140 ms tail feels responsive rather than floaty, and whether native
trackpad movement remains one-to-one. The performance numbers should be weighed
against that perceived improvement before this approach is merged.
