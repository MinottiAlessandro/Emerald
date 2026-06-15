# Emerald

A tiny, fast, Obsidian-style note app in C++/Qt. One dependency, plain Markdown
files, inline live preview. Built to stay small and maintainable.

## Features (v1)

- **Vault = a folder of `.md` files.** No database, no lock-in, Obsidian-compatible.
- **Inline live preview.** Headings, **bold**, *italic*, `code`, ~~strike~~ and
  `[[wiki-links]]` render in place; the syntax markers collapse away on every
  line except the one your cursor is on.
- **Full link system.** `[[Note]]` links are clickable (Ctrl+click), auto-create
  the target if it doesn't exist, and feed a live **Backlinks** panel. Typing
  `[[` pops a fuzzy autocomplete of existing note titles.
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

## Architecture

```
core/   no GUI, unit-testable
  Vault            scan a folder, read/write .md files, resolve link targets
  LinkIndex        parse [[links]] -> forward links + backlinks
  Note             { path, title }
ui/     Qt
  MainWindow       sidebar | editor | backlinks dock, autosave, navigation
  MarkdownEditor   QPlainTextEdit + Ctrl-click link nav, centered measure
  MarkdownHighlighter   inline live preview (conceal markers off the active line)
```

The `core/` layer depends only on `QtCore`, so the link logic can be tested
without a display.

## Roadmap

- Full-text search & tags
- Inline images / tables in preview
