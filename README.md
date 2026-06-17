<div align="center">

<img src="icons/EmeraldClean.png" alt="Emerald" width="128" height="128">

# Emerald

**A tiny, fast, Obsidian-style Markdown notes app.**

Plain `.md` files · inline live preview · one dependency · Linux · macOS · Windows

[![Release](https://github.com/MinottiAlessandro/Emerald/actions/workflows/release.yml/badge.svg)](https://github.com/MinottiAlessandro/Emerald/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/MinottiAlessandro/Emerald?sort=semver&color=2bbf74)](https://github.com/MinottiAlessandro/Emerald/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/MinottiAlessandro/Emerald/total?color=2bbf74)](https://github.com/MinottiAlessandro/Emerald/releases)
![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-2bbf74)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt6](https://img.shields.io/badge/Qt-6-41cd52?logo=qt&logoColor=white)
[![License: MIT](https://img.shields.io/badge/license-MIT-2bbf74)](LICENSE)

</div>

---

## Download

Grab the latest build for your platform — **self-contained, no Qt installation required.**

| Platform | Download | Notes |
|---|---|---|
| 🐧 **Linux** (x86-64) | [`Emerald-x86_64.AppImage`](https://github.com/MinottiAlessandro/Emerald/releases/latest/download/Emerald-x86_64.AppImage) | `chmod +x` then run |
| 🐧 **Linux** (ARM64) | [`Emerald-aarch64.AppImage`](https://github.com/MinottiAlessandro/Emerald/releases/latest/download/Emerald-aarch64.AppImage) | `chmod +x` then run · needs glibc ≥ 2.39 |
| 🍎 **macOS** (universal) | [`Emerald-macOS.dmg`](https://github.com/MinottiAlessandro/Emerald/releases/latest/download/Emerald-macOS.dmg) | first launch: **right-click → Open → Open** |
| 🪟 **Windows** | [`Emerald-win64.zip`](https://github.com/MinottiAlessandro/Emerald/releases/latest/download/Emerald-win64.zip) | extract and run `emerald.exe` |

> Every download bundles its own Qt runtime, so there's nothing else to install. Looking for older versions? See **[all releases](https://github.com/MinottiAlessandro/Emerald/releases)**.

> **macOS note:** the app is ad-hoc signed but not notarized (no paid Apple
> Developer ID), so the first launch needs **right-click → Open → Open**. If
> macOS still blocks it, clear the download quarantine in Terminal:
> ```bash
> xattr -cr /Applications/Emerald.app
> ```

---

## Why Emerald

- 🗂️ **Your notes are just files.** A vault is a folder of `.md` files — no database, no lock-in, fully Obsidian-compatible.
- ✨ **Live preview, in place.** Markdown renders as you type; the syntax markers melt away on every line except the one you're editing.
- 🔗 **Real `[[wiki-links]]`** with fuzzy autocomplete, auto-creation, and rename-aware backlink rewriting.
- 🔍 **Instant full-text search** over the whole vault, backed by an in-memory inverted index.
- 🪶 **One dependency (Qt 6), tiny footprint.** Built to stay small, fast, and maintainable.

---

## Features

**Editing & live preview**
- Headings, **bold**, *italic*, ***both***, `code`, ~~strike~~, ==highlight==, `> quotes`, `---` rules, `- [ ]` task lists, fenced ` ``` ` code blocks (with language tag), `|` tables and `[[wiki|links]]` all render in place.
- **Smart lists** — Enter continues a bullet / numbered / task list (numbers increment, indentation preserved); Enter on an empty item ends it; Tab / Shift+Tab indent and outdent. Off the active line, dashes become real bullet glyphs (●/○/▪ by nesting level).
- **Code folding** on headings and fenced blocks.

**Links & navigation**
- `[[Note]]` links are clickable (plain click once rendered, Ctrl+click on the line you're editing) and auto-create their target. `[[Note|alias]]` shows just the alias. Typing `[[` pops a fuzzy autocomplete of existing titles.
- **External links** — `[text](https://…)` renders as a clickable link (the `](url)` melts away, leaving just the text) and opens in your browser.
- **Title = filename** — the note's title is the first line above the body; editing it renames the file and rewrites every inbound `[[link]]`.
- **Back / forward history** like a browser (`Alt+←` / `Alt+→`, mouse side buttons, or the sidebar arrows).

**Vault & search**
- **Folder-tree sidebar** with drag-and-drop; right-click to create notes or sub-folders anywhere.
- **Telescope-style search popup** — ranked results, type to filter, ↑/↓ to move, Enter jumps to the first match.
- **Debounced autosave** plus **external-edit detection** — Emerald reloads notes changed outside the app.

**Polish**
- **No menubar** — a gear at the bottom of the sidebar holds settings and file actions; everything has a shortcut.
- **Adjustable editor font** (family + size, persisted); heading sizes scale with it.
- **Dark theme**, embedded in the binary.

---

## Keyboard shortcuts

> On macOS, use **⌘** where **Ctrl** is listed.

| Action | Shortcut |
|---|---|
| Open vault | `Ctrl+O` |
| Switch vault | `Ctrl+Shift+O` |
| New note | `Ctrl+N` |
| Go to note (quick open) | `Ctrl+P` |
| Save now | `Ctrl+S` |
| Delete note | `Ctrl+Delete` |
| Find in note | `Ctrl+F` |
| Search vault | `Ctrl+Shift+F` |
| Back / Forward | `Alt+←` / `Alt+→`  ·  `Ctrl+[` / `Ctrl+]` |
| Indent / outdent list item | `Tab` / `Shift+Tab` |
| Font size up / down / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| Quit | `Ctrl+Q` |

---

## Build from source

Requires **Qt 6**, a **C++20** compiler, and **CMake ≥ 3.21**.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/emerald
```

Open a vault with `Ctrl+O` and create your first note with `Ctrl+N`.

The widget backing store is composited on the GPU by default (Qt's RHI path) for
smooth resizing; set `QT_WIDGETS_RHI=0` to fall back to CPU raster.

### Packaging

The build produces platform-native packages via the install rules and CPack:

```bash
cmake --install build --prefix dist     # system-style install (Linux), .app (macOS), windeployqt (Windows)
cd build && cpack                        # → .tar.gz (Linux) / .dmg (macOS) / .zip (Windows)
./packaging/linux/make_appimage.sh       # portable Linux AppImage (via linuxdeploy)
```

Tagging a `vX.Y.Z` release triggers the [GitHub Actions workflow](.github/workflows/release.yml),
which builds all three platforms and attaches the packages to the release.

---

## Architecture

```
core/   no GUI, unit-testable (depends only on QtCore)
  Vault            scan a folder, read/write .md files, resolve link targets
  SearchIndex      inverted index for fast full-text search
  WikiLink         the shared [[wiki-link]] pattern + target cleaning
  Note             { path, title }
ui/     Qt Widgets
  MainWindow       folder tree + title + editor; autosave, history, rename
  MarkdownEditor   QPlainTextEdit + clickable links, [[ autocomplete, lists, folding
  MarkdownHighlighter   inline live preview (conceals markers off the active line)
  SearchPopup      centered Telescope-style search overlay
```

The `core/` layer is GUI-free, so the vault and link logic can be tested without a display.

---

## Roadmap

- [x] Cross-platform support (Linux · macOS · Windows)
- [x] Per-platform packaging & automated releases
- [ ] LaTeX math (`$$`) and Mermaid diagrams
- [ ] Tags
- [ ] Graphic bullet glyphs / inline images

---

## License

Released under the [MIT License](LICENSE) — © 2026 Alessandro Minotti.
