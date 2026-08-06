# Emerald Performance Benchmarks

Development-only benchmark suite for release-to-release performance tracking.
It uses only Qt and the existing Emerald code.

Build:

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DEMERALD_BUILD_PERF_TESTS=ON
cmake --build build-perf -j
```

Run:

```bash
QT_QPA_PLATFORM=offscreen ./build-perf/emerald_perf_tests --json perf-1.5.1.json
```

Check the current run against the CI absolute budgets:

```bash
python3 tests/perf/check_perf_budget.py perf-1.5.1.json
```

Compare a new release against an older baseline:

```bash
python3 tests/perf/check_perf_budget.py perf-1.5.1.json \
  --baseline perf-1.5.0.json
```

Useful knobs:

```bash
QT_QPA_PLATFORM=offscreen ./build-perf/emerald_perf_tests \
  --notes 10000 \
  --words 250 \
  --profile heavy \
  --queries 200 \
  --json perf-large.json
```

Profiles:

- `classic`: the original benchmark vault shape and the default used by CI;
  Markdown notes with links, occasional math/code blocks, task/list markup, and
  shallow nested folders.
- `light`: mostly plain Markdown notes, shallow folders, a small amount of
  linking and task/list markup.
- `mixed`: frontmatter, tags, aliases, nested folders, links, math, code blocks,
  task/list markup, and local attachment image references.
- `heavy`: the mixed profile plus deeper folders, more links, more unique
  tokens, longer notes, more math/code/tables, and more generated local image
  attachments.

Metrics:

- `generate_vault`: deterministic synthetic vault creation time.
- `vault_scan`: filesystem scan and note/folder listing time.
- `search_rebuild`: full index build time.
- `startup_index_first_chunk`: time spent indexing the first UI-friendly batch.
- `startup_index_complete`: total time to finish the chunked startup index.
- `startup_index_chunks`: number of startup batches needed.
- `search_p50` / `search_p95`: repeated search latency.
- `search_update_note`: incremental reindex time for one edited note.
- `editor_set_plain_text`: load a large note into the editor.
- `editor_render_viewport`: render the editor viewport once.
- `editor_build_read_document`: parse the same large Markdown note into the
  separate presentation-only Read Mode document and install it in the editor.
- `editor_render_read_viewport`: render one viewport of the separate Read Mode
  document, including any native math, code, task, rule, and image objects.
- `math_measure_and_paint`: repeated formula measure/paint loop.
- `mascot_render_unique_500`: render 500 different deterministic mascot pixmaps.
- `mascot_render_repeated_500`: render the same mascot 500 times to measure cache reuse.
- `rss_start_current`: resident memory after the benchmark creates `QApplication`.
- `rss_after_rebuild` / `rss_final`: observed peak RSS, using the higher value
  of the platform peak and the benchmark's own current-RSS snapshots.
- `rss_after_scan_current`, `rss_after_rebuild_current`,
  `rss_after_search_current`, `rss_after_editor_current`,
  `rss_after_mascot_current`: current resident memory snapshots after each
  major phase, where the platform exposes it.
- `rss_search_rebuild_delta`: current resident memory added by full search
  indexing, measured from just before to just after `search_rebuild`.

For consistent release comparisons, run on the same machine, same Qt version,
same build type, and close unrelated heavy processes. Use Release builds.
CI uses the `classic` profile in `perf-budget.json`: 2500 notes, 220 words per
note, and 80 query repetitions.
