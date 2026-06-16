# Emerald

A tiny, fast, Obsidian-style note app in C++/Qt. One dependency, plain Markdown
files, inline live preview. Built to stay small and maintainable.

## Features (v1)

- **Vault = a folder of `.md` files.** No database, no lock-in, Obsidian-compatible.
- **Inline live preview.** Headings, **bold**, *italic*, ***both***, `code`,
  ~~strike~~, ==highlight==, `> quotes`, `---` rules, `- [ ]` task lists,
  fenced ` ``` ` code blocks (with a language tag), `|` tables and
  `[[wiki|links]]` render in place; the syntax markers collapse away on every
  line except the one your cursor is on.
- **Full link system.** `[[Note]]` links are clickable — a plain click once
  the link is rendered, or Ctrl+click on the line you're editing — and
  auto-create the target if it doesn't exist. `[[Note|alias]]` shows just the
  alias. Typing `[[` pops a fuzzy autocomplete of existing note titles.
- **Smart lists.** Enter continues a bullet, numbered or `- [ ]` task list
  (numbers increment, indentation is kept); Enter on an empty item ends it.
  Tab / Shift+Tab indent and outdent the current item.
- **Title = filename.** The note's title is the first line above the body (the
  `.md` is never shown); editing it renames the file and rewrites every
  inbound `[[link]]` to match.
- **Fast full-text search** (`Ctrl+F`). A Telescope-style popup over the
  window, backed by an in-memory inverted index; type to filter, ↑/↓ to move,
  Enter to open. Results are ranked and opening one jumps to the first match.
- **Back / forward navigation** (`Alt+←` / `Alt+→`, the mouse side buttons, or
  the arrows in the sidebar header) walks the notes you've visited, like a
  browser.
- **No menubar.** A gear at the bottom of the sidebar holds settings and the
  file actions; everything also has a keyboard shortcut.
- **Adjustable font.** Pick the editor font family and size in Settings
  (persisted); heading sizes scale with it.
- **Debounced autosave.** Your notes are written to disk as you type.
- **Dark theme**, embedded in the binary.

## Build

Requires Qt 6 and a C++20 compiler.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/emerald
```

Open a vault with `Ctrl+O` (try `vault-demo/`), create notes with `Ctrl+N`.

The widget backing store is composited on the GPU by default (Qt's RHI path)
for smooth window resizing. Set `QT_WIDGETS_RHI=0` to fall back to CPU raster.

## Architecture

```
core/   no GUI, unit-testable
  Vault            scan a folder, read/write .md files, resolve link targets
  SearchIndex      inverted index for fast full-text search
  WikiLink         the shared [[wiki-link]] pattern + target cleaning
  Note             { path, title }
ui/     Qt
  MainWindow       sidebar (search + notes) | editor, autosave, history nav
  MarkdownEditor   QPlainTextEdit + Ctrl-click links, [[ autocomplete
  MarkdownHighlighter   inline live preview (conceal markers off the active line)
```

The `core/` layer depends only on `QtCore`, so the link logic can be tested
without a display.

## Roadmap

- Full-text search & tags
- Inline images / tables in preview
