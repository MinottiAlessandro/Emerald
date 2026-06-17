## Emerald 1.0.0

First stable, cross-platform release of Emerald — a minimal, fast, Obsidian-like
Markdown note editor built with Qt 6.

### Highlights
- **Runs on Linux, macOS and Windows** from a single Qt 6 Widgets codebase.
- Live Markdown preview with inline "markup melt", fenced code blocks and pipe tables.
- Wiki-style `[[links]]` with rename-aware backlink updates and browser-style history.
- Fast full-text search over the vault with an in-memory inverted index.
- Folder-tree sidebar, debounced autosave, external-edit file watching, dark theme.

### Cross-platform polish in this release
- Fixed-width fonts now resolve correctly on Windows/macOS (code blocks & tables stay aligned).
- Filename validation rejects Windows reserved names, trailing dots/spaces and control characters.
- Case-only note retitles work on case-insensitive filesystems (macOS/Windows).
- Notes are written with LF line endings everywhere, so vaults are byte-identical across OSes.

### Downloads
- **Linux** — `Emerald-1.0.0-x86_64.AppImage` (portable; `chmod +x` and run).
- **macOS** — `Emerald-1.0.0-Darwin.dmg` (drag to Applications).
- **Windows** — `Emerald-1.0.0-win64.zip` (extract and run `emerald.exe`).

Built automatically by the `Release` GitHub Actions workflow.
