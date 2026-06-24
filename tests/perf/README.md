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

Useful knobs:

```bash
QT_QPA_PLATFORM=offscreen ./build-perf/emerald_perf_tests \
  --notes 10000 \
  --words 250 \
  --queries 200 \
  --json perf-large.json
```

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
- `math_measure_and_paint`: repeated formula measure/paint loop.
- `mascot_render_500`: render 500 deterministic mascot pixmaps.
- `rss_after_rebuild` / `rss_final`: peak RSS where the platform exposes it.

For consistent release comparisons, run on the same machine, same Qt version,
same build type, and close unrelated heavy processes. Use Release builds.
