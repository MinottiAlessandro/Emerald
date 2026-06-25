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

<p align="center">
  <a href="https://ko-fi.com/alessandromino">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi" />
  </a>
</p>

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
- 🧮 **Math, built in.** Inline `$…$` and display `$$…$$` LaTeX render live — fractions, roots, matrices, accents — with no extra dependencies.
- 🐾 **A mascot per note.** Each note can grow its own little procedurally drawn creature in the corner — a memorable face to recall it by.
- 🪶 **One dependency (Qt 6), tiny footprint.** Built to stay small, fast, and maintainable.

---

## Features

**Editing & live preview**
- Headings, **bold**, *italic*, ***both***, `code`, ~~strike~~, ==highlight==, `> quotes`, `---` rules, `- [ ]` task lists, fenced ` ``` ` code blocks (with language tag), `|` tables and `[[wiki|links]]` all render in place.
- **Overlapping emphasis** — bold / italic / strike / highlight nest and stack, so `==dog ~~cat *horse **elephant***~~==` layers all four styles incrementally.
- **Pipe tables** — Enter on a header row auto-adds the `---` separator and a first data row (caret lands in the first cell); Tab walks/grows the grid and re-aligns the columns on every press; Enter on the last row leaves the table.
- **Smart lists** — Enter continues a bullet / numbered / task list (numbers increment, indentation preserved); pressing Enter mid-item splits the line, carrying the text after the caret onto a new marked item; Enter on an empty item ends it; Tab / Shift+Tab indent and outdent (a multi-line selection indents every line). Off the active line, dashes become real bullet glyphs (●/○/▪ by nesting level).
- **Ctrl+Enter** starts a new line below without splitting the current one — and keeps the list going (or clears an empty bullet).
- **Wrap the selection** — select text and press `(`, `[`, `*`, `_`, `=`, `'`, `"`, `` ` ``, `~` or `$` to surround it (brackets close with their match; `$` wraps a multi-line selection as one span).
- **Code folding** on headings and fenced blocks.

**Math** *(no dependencies — a small built-in TeX-subset typesetter)*
- **Inline `$…$`** and **display `$$…$$`** render live in place; a `$$` block can span several lines (open/close on their own lines or carrying content), and the raw source reappears whenever the caret or selection is inside it. Bare dollars (`$5 and $12`) stay literal.
- **Fractions** `\frac` `\dfrac` `\tfrac`, **roots** `\sqrt`, super/subscripts with **stacked limits** on big operators (`\sum` `\prod` `\int` `\bigcup` …) in display mode, and `\binom`.
- **Accents** (`\hat \bar \vec \tilde \dot \ddot \widehat \overline`), **grown delimiters** `\left( … \right]`, **`\text` / `\textbf` / `\operatorname`**, **matrices** (`pmatrix` `bmatrix` `vmatrix` …), ~150 symbol commands (Greek, operators, relations, arrows), and full Unicode (emoji, CJK).

**Links & navigation**
- `[[Note]]` links are clickable (plain click once rendered, Ctrl+click on the line you're editing) and auto-create their target. `[[Note|alias]]` shows just the alias. Typing `[[` pops a fuzzy autocomplete of existing titles.
- **External links** — `[text](https://…)` renders as a clickable link (the `](url)` melts away, leaving just the text) and opens in your browser.
- **Title = filename** — the note's title is the first line above the body; editing it renames the file and rewrites every inbound `[[link]]`.
- **Back / forward history** like a browser (`Alt+←` / `Alt+→`, mouse side buttons, or the sidebar arrows).

**Vault & search**
- **Folder-tree sidebar** with drag-and-drop; right-click to create notes or sub-folders anywhere.
- **Telescope-style search popup** — ranked results, type to filter, ↑/↓ to move, Enter jumps to the first match.
- **Templates** — point **Settings → Templates folder** at a folder in the vault, then **Insert Template…** (`Ctrl+T`) opens a quick picker of every note under it (sub-folders included) and drops the chosen one in at the caret. Templates can carry `{{date}}`, `{{time}}` and `{{title}}` placeholders — each filled in on insert; `{{date}}`/`{{time}}` take an optional Moment.js-style format after a colon (e.g. `{{date:YYYY/MM/DD}}`, `{{time:HH:mm:ss}}`).
- **Debounced autosave** plus **external-edit detection** — Emerald reloads notes changed outside the app.

**Mascots**
- **A creature per note** — an optional, procedurally drawn mascot in the bottom-right corner, seeded from the note's title and text (rendered live from the seed by default — no image files needed). Around three dozen archetypes spanning ordinary animals, mythological creatures, and objects. Hover for a gentle blink and bob.
- **Gallery & control** — click a mascot (or press **Ctrl+G**) for a vault-wide **gallery** (click any creature to jump to its note). **Generate** (**Ctrl+M**) / **Delete** (**Ctrl+Shift+M**) from the gear menu; auto-generation once a note passes a character count is opt-in under **Settings → Mascot**. Deleting a note's mascot stops auto-generation from recreating it for that note — generate one by hand (**Ctrl+M**) to resume.
- **Bring your own creatures** — drop a folder of SVGs into your mascots folder (`creatures/<name>/body.svg`, plus optional `topper`/`eyes`/`mouth`/… layers) and Emerald discovers it and starts rolling your creature into the mix alongside the built-ins — no rebuild. The chosen creature is recorded in the note so it stays reproducible and travels with the file, falling back to a built-in on a machine that doesn't have the art.
- **Or bring your own images** — prefer hand-drawn or AI-generated art? Drop square images (PNG/JPG/WEBP) into an `images/` folder inside your mascots folder and switch on **Use Image Mascots** in the gear menu. Each note then shows one of your images — picked deterministically by its seed — as a rounded tile instead of the procedural creature. Like custom creatures, the images live on your machine (not in the vault), so a note falls back to its procedural mascot where the images aren't present.
- **Stored in the note itself** — the seed lives in a hidden first line (`<!-- mascot: … -->`), invisible in any other Markdown viewer, so mascots travel with your notes and leave no separate metadata behind. Press **↑** at the top of a note to reveal the line; edit or delete the seed by hand and the creature follows.

**Polish**
- **No menubar** — a gear at the bottom of the sidebar holds settings and file actions; everything has a shortcut.
- **Adjustable editor font** (family + size, persisted) and **line spacing**; heading sizes scale with the font.
- **Self-updating** — **Check for Updates…** pulls the latest release from GitHub (installs in place on Linux AppImage).
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
| Insert template | `Ctrl+T` |
| Save now | `Ctrl+S` |
| Rename note | `F2` |
| Delete note | `Ctrl+Shift+Backspace` |
| Find in note | `Ctrl+F` |
| Search vault | `Ctrl+Shift+F` |
| Settings | `Ctrl+,` |
| Back / Forward | `Alt+←` / `Alt+→` |
| Bold / Italic | `Ctrl+B` / `Ctrl+I` |
| Insert link | `Ctrl+K` |
| Heading level 1–6 (press again to clear) | `Ctrl+1` … `Ctrl+6` |
| Select line | `Ctrl+L` |
| Move line up / down | `Alt+↑` / `Alt+↓` |
| Indent / outdent list item | `Tab` / `Shift+Tab` |
| Font size up / down / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| Toggle sidebar | `Ctrl+\` |
| Mascot gallery | `Ctrl+G` |
| Generate mascot | `Ctrl+M` |
| Delete mascot | `Ctrl+Shift+M` |
| Quit | `Ctrl+Q` |

> `Ctrl+Delete` stays the usual delete-word-forward; Delete note is the deliberate `Ctrl+Shift+Backspace`.

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

## License

Released under the [MIT License](LICENSE) — © 2026 Alessandro Minotti.
